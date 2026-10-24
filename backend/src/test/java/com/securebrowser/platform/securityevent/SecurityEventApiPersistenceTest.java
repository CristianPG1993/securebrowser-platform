package com.securebrowser.platform.securityevent;

import static org.assertj.core.api.Assertions.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.IntStream;
import com.securebrowser.platform.auth.AccessTokenService;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.device.Device;
import com.securebrowser.platform.device.DeviceRepository;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.securityevent.dto.*;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;
import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;
import org.aopalliance.intercept.MethodInterceptor;
import org.junit.jupiter.api.*;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.aop.framework.Advised;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.dao.DataIntegrityViolationException;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.http.MediaType;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.ResultActions;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import tools.jackson.databind.json.JsonMapper;

/** Recorre HTTP, permisos, lotes atómicos y carreras con servicios y PostgreSQL reales. */
@SpringBootTest(properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-events-test", "sbp.security.jwt.audience=sbp-events-api"
})
@AutoConfigureMockMvc
@Import({PostgresRepositoryTestConfiguration.class, SecurityEventApiPersistenceTest.FixedClockConfiguration.class})
class SecurityEventApiPersistenceTest {
    private static final Instant INITIAL = Instant.parse("2026-10-23T08:00:00Z");
    private static final Instant NOW = INITIAL.plusSeconds(300);
    private static final String HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    @Autowired private MockMvc mvc;
    @Autowired private JsonMapper mapper;
    @Autowired private AccessTokenService tokens;
    @Autowired private SecurityEventService service;
    @Autowired private SecurityEventRepository events;
    @Autowired private DeviceRepository devices;
    @Autowired private AuditingHandler auditing;
    @Autowired private JdbcTemplate jdbc;
    @Autowired private PlatformTransactionManager transactionManager;
    @PersistenceContext private EntityManager em;
    private Fixture f;

    @BeforeEach
    void prepare() {
        auditing.setDateTimeProvider(() -> Optional.of(INITIAL));
        f = tx().execute(status -> {
            Company a = new Company("Empresa A"); Company b = new Company("Empresa B");
            em.persist(a); em.persist(b);
            User admin = user(a, UserRole.ADMIN); User owner = user(a, UserRole.USER);
            User another = user(a, UserRole.USER); User foreign = user(b, UserRole.USER);
            Device first = device(a, owner, true); Device inactive = device(a, owner, false);
            Device other = device(a, another, true); Device foreignDevice = device(b, foreign, true);
            Device adminDevice = device(a, admin, true);
            em.flush();
            return new Fixture(actor(admin), actor(owner), actor(another), actor(foreign), first.getId(),
                    inactive.getId(), other.getId(), foreignDevice.getId(), adminDevice.getId());
        });
        auditing.setDateTimeProvider(() -> Optional.of(NOW));
    }

    @AfterEach
    void cleanup() {
        auditing.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        if (f == null) return;
        for (Long company : List.of(f.admin().companyId(), f.foreign().companyId())) {
            jdbc.update("delete from security_events where device_id in (select id from devices where company_id=?)", company);
            jdbc.update("delete from devices where company_id=?", company);
            jdbc.update("delete from enrollment_tokens where user_id in (select id from users where company_id=?)", company);
            jdbc.update("delete from policies where company_id=?", company);
            jdbc.update("delete from licenses where company_id=?", company);
            jdbc.update("delete from users where company_id=?", company);
            jdbc.update("delete from companies where id=?", company);
        }
    }

    @Test
    void receivesExactContractAndReplayPreservesFirstReceptionAndContent() throws Exception {
        UUID uuid = UUID.randomUUID();
        Map<String, Object> event = event(uuid, "URL_BLOCKED", "  Original\n😀 ", INITIAL.plusSeconds(600).toString());
        send(f.owner(), f.first(), body(event, event)).andExpect(status().isOk())
                .andExpect(jsonPath("$.acceptedEventUuids.length()").value(1))
                .andExpect(jsonPath("$.acceptedEventUuids[0]").value(uuid.toString()));
        Map<String, Object> stored = jdbc.queryForMap("select * from security_events where event_uuid=?", uuid);
        send(f.owner(), f.first(), body(event)).andExpect(status().isOk());
        // El reenvío idéntico conserva todos los datos del primer registro y no crea otra fila.
        assertThat(jdbc.queryForList("select * from security_events where device_id=?", f.first())).containsExactly(stored);
        // La primera recepción usa el reloj del backend, aunque Desktop informe una fecha futura.
        assertThat(((java.sql.Timestamp) stored.get("received_at")).toInstant()).isEqualTo(NOW);
        // La sincronización confirmada registra la actividad del servidor.
        assertThat(((java.sql.Timestamp) deviceState(f.first()).get("last_seen_at")).toInstant()).isEqualTo(NOW);
        String result = list(f.admin(), "?deviceId=" + f.first()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].details").value("  Original\n😀 "))
                .andExpect(jsonPath("$.items[0].occurredAt").value(INITIAL.plusSeconds(600).toString()))
                .andReturn().getResponse().getContentAsString();
        Map<?, ?> page = mapper.readValue(result, Map.class);
        Map<?, ?> item = (Map<?, ?>) ((List<?>) page.get("items")).getFirst();
        // El listado expone exactamente PageResponse.
        assertThat(page.keySet()).isEqualTo(Set.of("items", "page", "size", "totalElements", "totalPages"));
        // El evento expone solo los siete campos acordados, sin datos de usuario ni entidades.
        assertThat(item.keySet()).isEqualTo(Set.of("id", "eventUuid", "deviceId", "type", "details", "occurredAt", "receivedAt"));
        String accepted = send(f.owner(), f.first(), body(event)).andReturn().getResponse().getContentAsString();
        // La confirmación solo incluye la lista de UUID reconocidos.
        assertThat(mapper.readValue(accepted, Map.class).keySet()).isEqualTo(Set.of("acceptedEventUuids"));
    }

    @Test
    void replayKeepsEarlierReceptionAndUsesDomainMicrosecondPrecision() throws Exception {
        UUID uuid = UUID.randomUUID();
        Instant occurred = INITIAL.plusNanos(123456000);
        tx().executeWithoutResult(status -> em.persist(new SecurityEvent(uuid, em.getReference(Device.class, f.first()),
                SecurityEventType.URL_BLOCKED, null, occurred, INITIAL)));
        Map<String, Object> item = event(uuid, "URL_BLOCKED", null, "2026-10-23T08:00:00.123456789+00:00");
        item.remove("details");
        send(f.owner(), f.first(), body(item)).andExpect(status().isOk());
        Map<String, Object> stored = jdbc.queryForMap("select * from security_events where event_uuid=?", uuid);
        // Un reenvío posterior conserva la recepción inicial, anterior al Clock actual.
        assertThat(((java.sql.Timestamp) stored.get("received_at")).toInstant()).isEqualTo(INITIAL);
        // La comparación de occurredAt usa la precisión de microsegundos ya acordada para la entidad.
        assertThat(((java.sql.Timestamp) stored.get("occurred_at")).toInstant()).isEqualTo(occurred);
        // Omitir details equivale a null y no provoca un conflicto artificial.
        assertThat(count(f.first())).isEqualTo(1);
    }

    @Test
    void preservesFirstAppearanceOrderAndNullableOrUnnormalizedDetails() throws Exception {
        Map<String, Object> first = event(UUID.fromString("ffffffff-ffff-4fff-8fff-ffffffffffff"), "URL_BLOCKED", null, INITIAL.toString());
        Map<String, Object> second = event(UUID.fromString("00000000-0000-4000-8000-000000000001"), "DOWNLOAD_BLOCKED", "", INITIAL.toString());
        send(f.owner(), f.first(), body(first, second, first)).andExpect(status().isOk())
                .andExpect(jsonPath("$.acceptedEventUuids[0]").value(first.get("eventUuid")))
                .andExpect(jsonPath("$.acceptedEventUuids[1]").value(second.get("eventUuid")))
                .andExpect(jsonPath("$.acceptedEventUuids.length()").value(2));
        list(f.owner(), "?deviceId=" + f.first()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items.length()").value(2));
        // Null se conserva como ausencia de detalles, sin convertirlo en cadena vacía.
        assertThat(jdbc.queryForObject("select details from security_events where event_uuid=?",
                String.class, UUID.fromString((String) first.get("eventUuid")))).isNull();
        // Una cadena vacía es un contenido distinto y se conserva.
        assertThat(jdbc.queryForObject("select details from security_events where event_uuid=?",
                String.class, UUID.fromString((String) second.get("eventUuid")))).isEmpty();
    }

    @Test
    void acceptsHundredEventsUnicodeLimitAndInactiveDevicesWithoutReactivation() throws Exception {
        Map<String, Object> before = deviceState(f.inactive());
        List<Map<String, Object>> batch = IntStream.range(0, 100).mapToObj(i -> event(UUID.randomUUID(),
                "POLICY_UPDATED", "😀".repeat(2000), INITIAL.toString())).toList();
        send(f.owner(), f.inactive(), mapper.writeValueAsString(Map.of("events", batch))).andExpect(status().isOk())
                .andExpect(jsonPath("$.acceptedEventUuids.length()").value(100));
        list(f.owner(), "?deviceId=" + f.inactive() + "&size=100").andExpect(status().isOk())
                .andExpect(jsonPath("$.totalElements").value(100));
        // Recibir historial pendiente mantiene la instalación inactiva y conserva sus referencias.
        assertThat(deviceState(f.inactive())).containsEntry("active", false)
                .containsEntry("license_id", before.get("license_id")).containsEntry("policy_id", before.get("policy_id"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"type", "details", "occurredAt", "nullToEmpty"})
    void contentConflictRollsBackNewPrefixAndActivity(String changed) throws Exception {
        UUID existing = UUID.fromString("ffffffff-ffff-4fff-8fff-ffffffffffff");
        Map<String, Object> original = event(existing, "URL_BLOCKED", null, INITIAL.toString());
        send(f.owner(), f.first(), body(original)).andExpect(status().isOk());
        Map<String, Object> before = deviceState(f.first());
        Map<String, Object> stored = jdbc.queryForMap("select * from security_events where event_uuid=?", existing);
        Map<String, Object> conflict = new LinkedHashMap<>(original);
        switch (changed) {
            case "type" -> conflict.put("type", "DOWNLOAD_BLOCKED");
            case "details" -> conflict.put("details", "changed");
            case "nullToEmpty" -> conflict.put("details", "");
            default -> conflict.put("occurredAt", INITIAL.plusSeconds(1).toString());
        }
        UUID newUuid = UUID.fromString("00000000-0000-4000-8000-000000000001");
        send(f.owner(), f.first(), body(event(newUuid, "URL_BLOCKED", null, INITIAL.toString()), conflict))
                .andExpect(status().isConflict()).andExpect(jsonPath("$.code").value("EVENT_UUID_CONFLICT"));
        // La inserción anterior al conflicto se revierte junto con el lote completo.
        assertThat(events.findByEventUuid(newUuid)).isEmpty();
        // El evento que ya existía permanece íntegro.
        assertThat(jdbc.queryForMap("select * from security_events where event_uuid=?", existing)).isEqualTo(stored);
        // El rechazo tampoco cambia la auditoría ni lastSeenAt.
        assertThat(deviceState(f.first())).isEqualTo(before);
    }

    @Test
    void rejectsConflictingDuplicatesInsideBatchAndInvalidSuffixAtomically() throws Exception {
        UUID uuid = UUID.randomUUID();
        Map<String, Object> before = deviceState(f.first());
        send(f.owner(), f.first(), body(event(uuid, "URL_BLOCKED", "one", INITIAL.toString()),
                event(uuid, "URL_BLOCKED", "two", INITIAL.toString())))
                .andExpect(status().isConflict()).andExpect(jsonPath("$.code").value("EVENT_UUID_CONFLICT"));
        send(f.owner(), f.first(), body(event(uuid, "URL_BLOCKED", null, INITIAL.toString()), Map.of("type", "INVALID")))
                .andExpect(status().isBadRequest());
        // Ninguna petición incompleta o contradictoria deja nuevos eventos.
        assertThat(count(f.first())).isZero();
        // Tampoco se registra actividad por un lote rechazado.
        assertThat(deviceState(f.first())).isEqualTo(before);
    }

    @Test
    void requiresOwnershipBeforeDeduplicationAndKeepsGlobalUuidPrivate() throws Exception {
        UUID uuid = UUID.randomUUID();
        String payload = body(event(uuid, "URL_BLOCKED", "private", INITIAL.toString()));
        send(f.owner(), f.first(), payload).andExpect(status().isOk());
        for (Long id : List.of(f.other(), f.foreignDevice(), Long.MAX_VALUE)) {
            send(f.owner(), id, payload).andExpect(status().isNotFound())
                    .andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
        }
        send(f.admin(), f.first(), payload).andExpect(status().isNotFound());
        Map<String, Object> before = deviceState(f.foreignDevice());
        String error = send(f.foreign(), f.foreignDevice(), payload).andExpect(status().isConflict())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON))
                .andExpect(jsonPath("$.code").value("EVENT_UUID_CONFLICT"))
                .andReturn().getResponse().getContentAsString();
        // El conflicto global no revela el UUID, el detalle privado ni nombres SQL.
        assertThat(error).doesNotContain("private", uuid.toString(), "security_events", "deviceId");
        // Otro Device autorizado no puede apropiarse del UUID ya registrado.
        assertThat(count(f.foreignDevice())).isZero();
        // El conflicto conserva la actividad del Device que intentó enviar.
        assertThat(deviceState(f.foreignDevice())).isEqualTo(before);
        send(f.admin(), f.adminDevice(), body(event(UUID.randomUUID(), "URL_BLOCKED", null, INITIAL.toString())))
                .andExpect(status().isOk());
    }

    @Test
    void filtersPagesAndTotalsByCompanyOwnerTypeAndHalfOpenDates() throws Exception {
        UUID old = UUID.randomUUID(); UUID firstTie = UUID.randomUUID(); UUID secondTie = UUID.randomUUID();
        seed(f.first(), old, "URL_BLOCKED", INITIAL.minusSeconds(1));
        seed(f.first(), firstTie, "URL_BLOCKED", INITIAL);
        seed(f.first(), secondTie, "DOWNLOAD_BLOCKED", INITIAL);
        seed(f.inactive(), UUID.randomUUID(), "POLICY_UPDATED", INITIAL.plusSeconds(1));
        seed(f.other(), UUID.randomUUID(), "URL_BLOCKED", INITIAL);
        seed(f.foreignDevice(), UUID.randomUUID(), "URL_BLOCKED", INITIAL);
        Map<String, Object> before = deviceState(f.first());
        list(f.owner(), "?size=2").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(4))
                .andExpect(jsonPath("$.totalPages").value(2)).andExpect(jsonPath("$.items[1].eventUuid").value(secondTie.toString()));
        list(f.owner(), "?size=2&page=1").andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].eventUuid").value(firstTie.toString()))
                .andExpect(jsonPath("$.items[1].eventUuid").value(old.toString()));
        list(f.owner(), "?size=2&page=9").andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalElements").value(4));
        list(f.admin(), "?size=2").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(5));
        list(f.foreign(), "").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1));
        list(f.admin(), "?deviceId=" + f.first() + "&type=URL_BLOCKED&from=" + INITIAL + "&to=" + INITIAL.plusSeconds(1))
                .andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1))
                .andExpect(jsonPath("$.items[0].eventUuid").value(firstTie.toString()));
        list(f.owner(), "?from=" + INITIAL).andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(3));
        list(f.owner(), "?to=" + INITIAL).andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1));
        list(f.owner(), "?type=POLICY_UPDATED").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1));
        list(f.admin(), "?deviceId=" + f.adminDevice()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalPages").value(0));
        // Las lecturas del historial no simulan comunicaciones del dispositivo.
        assertThat(deviceState(f.first())).isEqualTo(before);
    }

    @Test
    void resolvesDeviceFilterWithinPermittedScope() throws Exception {
        for (Long id : List.of(f.other(), f.foreignDevice(), Long.MAX_VALUE)) {
            list(f.owner(), "?deviceId=" + id).andExpect(status().isNotFound());
        }
        for (Long id : List.of(f.foreignDevice(), Long.MAX_VALUE)) {
            list(f.admin(), "?deviceId=" + id).andExpect(status().isNotFound());
        }
        list(f.admin(), "?deviceId=" + f.other()).andExpect(status().isOk());
        list(f.owner(), "?deviceId=" + f.inactive()).andExpect(status().isOk());
    }

    @ParameterizedTest
    @ValueSource(strings = {"page=-1", "page=invalid", "page=2147483648", "size=0", "size=101", "size=1.5",
            "deviceId=0", "deviceId=-1", "deviceId=no", "deviceId=9223372036854775808", "deviceId=",
            "type=no", "type=url_blocked", "type=", "from=2026-10-23", "from=no",
            "from=2026-10-23T08:00:00", "from=2026-10-23T08:00:00%2B01:00", "to=",
            "from=2026-10-23T08:00:00Z&to=2026-10-23T08:00:00Z",
            "from=2026-10-24T08:00:00Z&to=2026-10-23T08:00:00Z", "sort=id", "companyId=1", "userId=1",
            "page=0&page=1", "from=2026-10-23T08:00:00Z&from=2026-10-23T09:00:00Z"})
    void rejectsInvalidQueryParameters(String query) throws Exception {
        list(f.owner(), "?" + query).andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"{}", "null", "[]", "{", "{\"events\":null}", "{\"events\":[]}",
            "{\"events\":{}}", "{\"events\":[null]}", "{\"events\":[{}]}", "{\"events\":[1]}",
            "{\"events\":[],\"companyId\":1}"})
    void rejectsMalformedOrMissingBatch(String json) throws Exception {
        Map<String, Object> before = deviceState(f.first());
        send(f.owner(), f.first(), json).andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
        // El cuerpo rechazado no crea eventos ni registra actividad.
        assertThat(count(f.first())).isZero();
        // La auditoría del Device permanece intacta.
        assertThat(deviceState(f.first())).isEqualTo(before);
    }

    @ParameterizedTest
    @ValueSource(strings = {"eventUuidMissing", "eventUuidNull", "eventUuidNumber", "eventUuidShort", "typeMissing",
            "typeNull", "typeNumber", "typeLowercase", "occurredAtMissing", "occurredAtNull", "occurredAtNumber",
            "occurredAtLocal", "occurredAtOffset", "detailsNumber", "detailsBoolean", "detailsTooLong", "deviceId", "receivedAt", "companyId"})
    void rejectsInvalidEventFieldsAndServerFields(String field) throws Exception {
        Map<String, Object> item = event(UUID.randomUUID(), "URL_BLOCKED", null, INITIAL.toString());
        switch (field) {
            case "eventUuidMissing" -> item.remove("eventUuid");
            case "eventUuidNull" -> item.put("eventUuid", null);
            case "eventUuidNumber" -> item.put("eventUuid", 42);
            case "eventUuidShort" -> item.put("eventUuid", "1-1-1-1-1");
            case "typeMissing" -> item.remove("type");
            case "typeNull" -> item.put("type", null);
            case "typeNumber" -> item.put("type", 1);
            case "typeLowercase" -> item.put("type", "url_blocked");
            case "occurredAtMissing" -> item.remove("occurredAt");
            case "occurredAtNull" -> item.put("occurredAt", null);
            case "occurredAtNumber" -> item.put("occurredAt", 1234);
            case "occurredAtLocal" -> item.put("occurredAt", "2026-10-23T08:00:00");
            case "occurredAtOffset" -> item.put("occurredAt", "2026-10-23T08:00:00+01:00");
            case "detailsNumber" -> item.put("details", 1);
            case "detailsBoolean" -> item.put("details", true);
            case "detailsTooLong" -> item.put("details", "😀".repeat(2001));
            default -> item.put(field, "server field");
        }
        send(f.owner(), f.first(), body(item)).andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
        // No se acepta una parte de un evento inválido.
        assertThat(count(f.first())).isZero();
    }

    @Test
    void rejectsOversizedBatchUnknownQueryAndInvalidIds() throws Exception {
        var item = event(UUID.randomUUID(), "URL_BLOCKED", null, INITIAL.toString());
        send(f.owner(), f.first(), mapper.writeValueAsString(Map.of("events", Collections.nCopies(101, item))))
                .andExpect(status().isBadRequest());
        for (String id : List.of("0", "-1", "invalid", "9223372036854775808")) {
            mvc.perform(post(route(id)).header("Authorization", bearer(f.owner())).contentType(MediaType.APPLICATION_JSON)
                    .content(body(item))).andExpect(status().isBadRequest());
        }
        mvc.perform(post(route(f.first().toString()) + "?deviceId=1").header("Authorization", bearer(f.owner()))
                .contentType(MediaType.APPLICATION_JSON).content(body(item))).andExpect(status().isBadRequest());
    }

    @Test
    void requiresBearerAndKeepsEventMutationEndpointsClosed() throws Exception {
        mvc.perform(get("/api/v1/security-events")).andExpect(status().isUnauthorized())
                .andExpect(header().string("WWW-Authenticate", "Bearer realm=\"sbp\""));
        mvc.perform(post(route(f.first().toString())).contentType(MediaType.APPLICATION_JSON).content("{}"))
                .andExpect(status().isUnauthorized());
        for (String method : List.of("POST", "PATCH", "DELETE")) {
            mvc.perform(request(org.springframework.http.HttpMethod.valueOf(method), "/api/v1/security-events")
                    .header("Authorization", bearer(f.admin()))).andExpect(status().isForbidden());
        }
    }

    @Test
    void enforcesDirectServiceGuardsWithoutController() {
        // Una invocación directa tampoco puede omitir la identidad autenticada.
        assertThatThrownBy(() -> service.list(null, 0, 20, null, null, null, null)).isInstanceOf(AccessDeniedException.class);
        // La recepción directa conserva el mismo control de identidad.
        assertThatThrownBy(() -> service.receive(null, f.first(), null)).isInstanceOf(AccessDeniedException.class);
        // Los parámetros inválidos se comprueban también en Service.
        assertThatThrownBy(() -> service.list(f.owner(), -1, 20, null, null, null, null))
                .isInstanceOf(InvalidSecurityEventRequestException.class);
        // El servicio no acepta un lote ausente.
        assertThatThrownBy(() -> service.receive(f.owner(), f.first(), null)).isInstanceOf(InvalidSecurityEventRequestException.class);
    }

    @Test
    void rollsBackInsertsAndActivityAfterRealFlushFailure() throws Exception {
        Map<String, Object> before = deviceState(f.first());
        Advised proxy = (Advised) devices;
        AtomicBoolean flushed = new AtomicBoolean();
        MethodInterceptor fail = invocation -> {
            Object result = invocation.proceed();
            if (invocation.getMethod().getName().equals("flush")) {
                flushed.set(true);
                throw new DataIntegrityViolationException("Simulated post-flush failure");
            }
            return result;
        };
        proxy.addAdvice(0, fail);
        try {
            send(f.owner(), f.first(), body(event(UUID.randomUUID(), "URL_BLOCKED", null, INITIAL.toString())))
                    .andExpect(status().isInternalServerError()).andExpect(jsonPath("$.code").value("INTERNAL_ERROR"));
        } finally { proxy.removeAdvice(fail); }
        // El fallo ocurre después de ejecutar SQL real del lote y del Device.
        assertThat(flushed).isTrue();
        // Ningún evento queda confirmado tras el fallo de la transacción.
        assertThat(count(f.first())).isZero();
        // La auditoría y la actividad también se revierten.
        assertThat(deviceState(f.first())).isEqualTo(before);
    }

    @Test
    void simultaneousIdenticalReplayCanRetryAfterDeviceContention() throws Exception {
        CountDownLatch inserted = new CountDownLatch(1); CountDownLatch release = new CountDownLatch(1);
        AtomicBoolean first = new AtomicBoolean(true);
        Advised proxy = (Advised) events;
        MethodInterceptor pause = invocation -> {
            Object result = invocation.proceed();
            if (invocation.getMethod().getName().equals("insertIfAbsent") && first.compareAndSet(true, false)) {
                inserted.countDown(); await(release);
            }
            return result;
        };
        String payload = body(event(UUID.randomUUID(), "URL_BLOCKED", null, INITIAL.toString()));
        proxy.addAdvice(0, pause);
        try (var executor = Executors.newSingleThreadExecutor()) {
            var winner = executor.submit(() -> send(f.owner(), f.first(), payload).andReturn().getResponse().getStatus());
            try {
                await(inserted);
                send(f.owner(), f.first(), payload).andExpect(status().isServiceUnavailable())
                        .andExpect(jsonPath("$.code").value("SERVICE_UNAVAILABLE"));
            } finally { release.countDown(); }
            // La primera petición puede confirmar normalmente después de liberar la coordinación.
            assertThat(winner.get(10, TimeUnit.SECONDS)).isEqualTo(200);
        } finally { release.countDown(); proxy.removeAdvice(pause); }
        send(f.owner(), f.first(), payload).andExpect(status().isOk());
        // Reintentar tras la contención reconoce un único evento, sin perder la confirmación.
        assertThat(count(f.first())).isEqualTo(1);
    }

    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void simultaneousGlobalUuidCollisionReturnsConflictAndRollsBackLosingBatch(boolean foreignCompany) throws Exception {
        UUID shared = UUID.fromString("ffffffff-ffff-4fff-8fff-ffffffffffff");
        UUID prefix = UUID.fromString("00000000-0000-4000-8000-000000000001");
        Long loserDevice = foreignCompany ? f.foreignDevice() : f.other();
        AuthenticatedUser loserActor = foreignCompany ? f.foreign() : f.another();
        Map<String, Object> before = deviceState(loserDevice);
        CountDownLatch inserted = new CountDownLatch(1); CountDownLatch release = new CountDownLatch(1);
        CountDownLatch attemptingConflict = new CountDownLatch(1);
        AtomicBoolean first = new AtomicBoolean(true);
        Advised proxy = (Advised) events;
        MethodInterceptor pause = invocation -> {
            boolean insert = invocation.getMethod().getName().equals("insertIfAbsent");
            if (insert && invocation.getArguments()[0].equals(shared)
                    && invocation.getArguments()[1].equals(loserDevice)) attemptingConflict.countDown();
            Object result = invocation.proceed();
            if (insert && invocation.getArguments()[1].equals(f.first()) && first.compareAndSet(true, false)) {
                inserted.countDown(); await(release);
            }
            return result;
        };
        var common = event(shared, "URL_BLOCKED", "original", INITIAL.toString());
        proxy.addAdvice(0, pause);
        try (var executor = Executors.newFixedThreadPool(2)) {
            var winner = executor.submit(() -> send(f.owner(), f.first(), body(common)).andReturn().getResponse().getStatus());
            Future<Integer> loser = null;
            try {
                await(inserted);
                loser = executor.submit(() -> send(loserActor, loserDevice,
                        body(event(prefix, "POLICY_UPDATED", null, INITIAL.toString()), common)).andReturn().getResponse().getStatus());
                await(attemptingConflict);
                Future<Integer> pending = loser;
                // La segunda transacción espera al UUID no confirmado de la primera.
                assertThatThrownBy(() -> pending.get(200, TimeUnit.MILLISECONDS)).isInstanceOf(TimeoutException.class);
            } finally { release.countDown(); }
            // El ganador confirma la primera recepción.
            assertThat(winner.get(10, TimeUnit.SECONDS)).isEqualTo(200);
            // El perdedor obtiene el conflicto de negocio después de leer al ganador confirmado.
            assertThat(loser.get(10, TimeUnit.SECONDS)).isEqualTo(409);
        } finally { release.countDown(); proxy.removeAdvice(pause); }
        // La unicidad global conserva un único evento para el Device original.
        assertThat(count(f.first())).isEqualTo(1);
        // También se revierte la inserción nueva anterior al UUID en conflicto del perdedor.
        assertThat(count(loserDevice)).isZero();
        // La petición perdedora no registra actividad.
        assertThat(deviceState(loserDevice)).isEqualTo(before);
    }

    private User user(Company company, UserRole role) {
        User user = new User("Ana", "Demo", UUID.randomUUID() + "@example.com", HASH, role, company);
        em.persist(user); return user;
    }
    private Device device(Company company, User owner, boolean active) {
        License license = new License(company, 5); Policy policy = new Policy("Política", company);
        em.persist(license); em.persist(policy);
        EnrollmentToken enrollment = new EnrollmentToken(license, owner, policy,
                UUID.randomUUID().toString().replace("-", "").repeat(2), INITIAL, INITIAL.plusSeconds(3600));
        enrollment.markUsed(INITIAL); em.persist(enrollment);
        Device device = new Device(UUID.randomUUID().toString(), null, company, owner, license, policy, enrollment);
        if (!active) device.deactivate();
        em.persist(device); return device;
    }
    private AuthenticatedUser actor(User user) {
        return new AuthenticatedUser(user.getId(), user.getCompany().getId(), user.getRole());
    }
    private void seed(Long device, UUID uuid, String type, Instant occurred) {
        tx().executeWithoutResult(status -> em.persist(new SecurityEvent(uuid, em.getReference(Device.class, device),
                SecurityEventType.valueOf(type), null, occurred, NOW)));
    }
    private Map<String, Object> event(UUID uuid, String type, String details, String occurred) {
        Map<String, Object> item = new LinkedHashMap<>();
        item.put("eventUuid", uuid.toString()); item.put("type", type); item.put("occurredAt", occurred); item.put("details", details);
        return item;
    }
    @SafeVarargs
    private final String body(Map<String, Object>... items) { return mapper.writeValueAsString(Map.of("events", List.of(items))); }
    private String bearer(AuthenticatedUser actor) {
        return "Bearer " + tokens.issue(actor.userId(), NOW.plusSeconds(604800)).getTokenValue();
    }
    private static String route(String id) { return "/api/v1/devices/" + id + "/events/batch"; }
    private ResultActions send(AuthenticatedUser actor, Long device, String json) throws Exception {
        return mvc.perform(post(route(device.toString())).header("Authorization", bearer(actor))
                .contentType(MediaType.APPLICATION_JSON).content(json));
    }
    private ResultActions list(AuthenticatedUser actor, String query) throws Exception {
        return mvc.perform(get("/api/v1/security-events" + query).header("Authorization", bearer(actor)));
    }
    private long count(Long device) { return jdbc.queryForObject("select count(*) from security_events where device_id=?", Long.class, device); }
    private Map<String, Object> deviceState(Long id) { return jdbc.queryForMap("select * from devices where id=?", id); }
    private TransactionTemplate tx() {
        TransactionTemplate tx = new TransactionTemplate(transactionManager); tx.setTimeout(10); return tx;
    }
    private static void await(CountDownLatch latch) {
        try { if (!latch.await(10, TimeUnit.SECONDS)) throw new IllegalStateException("Test coordination timed out"); }
        catch (InterruptedException exception) { Thread.currentThread().interrupt(); throw new IllegalStateException(exception); }
    }
    private record Fixture(AuthenticatedUser admin, AuthenticatedUser owner, AuthenticatedUser another, AuthenticatedUser foreign,
            Long first, Long inactive, Long other, Long foreignDevice, Long adminDevice) {}
    @TestConfiguration(proxyBeanMethods = false)
    static class FixedClockConfiguration {
        @Bean @Primary Clock eventTestClock() { return Clock.fixed(NOW, ZoneOffset.UTC); }
    }
}
