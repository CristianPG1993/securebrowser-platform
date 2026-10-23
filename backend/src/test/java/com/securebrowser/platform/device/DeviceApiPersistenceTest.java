package com.securebrowser.platform.device;

import static org.assertj.core.api.Assertions.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;
import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import com.securebrowser.platform.auth.AccessTokenService;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.policy.PolicyService;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.policy.dto.PolicyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;
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
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.ResultActions;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import tools.jackson.databind.json.JsonMapper;

/** Comprueba alcance, propiedad, snapshots y actividad con servicios y PostgreSQL reales. */
@SpringBootTest(properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-device-test", "sbp.security.jwt.audience=sbp-device-api",
        "sbp.security.jwt.max-lifetime-seconds=900"
})
@AutoConfigureMockMvc
@Import({PostgresRepositoryTestConfiguration.class, DeviceApiPersistenceTest.FixedClockConfiguration.class})
class DeviceApiPersistenceTest {
    private static final String BASE = "/api/v1/devices";
    private static final Instant INITIAL = Instant.parse("2026-10-23T08:00:00Z");
    private static final Instant NOW = INITIAL.plusSeconds(300);
    private static final String HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    @Autowired private MockMvc mvc;
    @Autowired private JsonMapper mapper;
    @Autowired private DeviceService service;
    @Autowired private DeviceRepository repository;
    @Autowired private PolicyService policies;
    @Autowired private AccessTokenService tokens;
    @Autowired private Clock clock;
    @Autowired private AuditingHandler auditing;
    @Autowired private JdbcTemplate jdbc;
    @Autowired private PlatformTransactionManager transactionManager;
    @PersistenceContext private EntityManager em;
    private Fixture fixture;

    @BeforeEach
    void prepare() {
        auditing.setDateTimeProvider(() -> Optional.of(INITIAL));
        fixture = tx().execute(status -> {
            Company a = new Company("Empresa A");
            Company b = new Company("Empresa B");
            em.persist(a);
            em.persist(b);
            User admin = user(a, UserRole.ADMIN);
            User owner = user(a, UserRole.USER);
            User another = user(a, UserRole.USER);
            User foreign = user(b, UserRole.USER);
            License licenseA = new License(a, 5);
            License licenseB = new License(b, 5);
            Policy policyA = new Policy("Política A", a);
            policyA.addUrlRule("blocked.example");
            policyA.addDownloadRule("exe");
            Policy policyB = new Policy("Política B", b);
            em.persist(licenseA); em.persist(licenseB);
            em.persist(policyA); em.persist(policyB);
            Device first = device(a, owner, licenseA, policyA, true);
            Device otherCompany = device(b, foreign, licenseB, policyB, true);
            Device anotherOwner = device(a, another, licenseA, policyA, true);
            Device inactive = device(a, owner, licenseA, policyA, false);
            Device adminOwned = device(a, admin, licenseA, policyA, true);
            em.flush();
            return new Fixture(actor(admin), actor(owner), actor(another), actor(foreign),
                    first.getId(), inactive.getId(), anotherOwner.getId(), otherCompany.getId(),
                    adminOwned.getId(), policyA.getId());
        });
        auditing.setDateTimeProvider(() -> Optional.of(NOW));
    }

    @AfterEach
    void cleanup() {
        auditing.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        if (fixture != null) {
            for (Long company : List.of(fixture.admin().companyId(), fixture.foreign().companyId())) {
                jdbc.update("delete from devices where company_id=?", company);
                jdbc.update("delete from enrollment_tokens where user_id in (select id from users where company_id=?)", company);
                jdbc.update("delete from policies where company_id=?", company);
                jdbc.update("delete from licenses where company_id=?", company);
                jdbc.update("delete from users where company_id=?", company);
                jdbc.update("delete from companies where id=?", company);
            }
        }
    }

    @Test
    void adminListsCompanyAndUserOnlyOwnDevicesWithExactDto() throws Exception {
        String body = list(fixture.owner(), "").andExpect(status().isOk())
                .andExpect(jsonPath("$.items.length()").value(2)).andExpect(jsonPath("$.totalElements").value(2))
                .andExpect(jsonPath("$.items[0].id").value(fixture.first()))
                .andExpect(jsonPath("$.items[1].id").value(fixture.inactive()))
                .andExpect(jsonPath("$.items[0].name").isEmpty())
                .andExpect(jsonPath("$.items[0].lastSeenAt").isEmpty())
                .andExpect(jsonPath("$.page").value(0)).andExpect(jsonPath("$.size").value(20))
                .andReturn().getResponse().getContentAsString();
        Map<?, ?> page = mapper.readValue(body, Map.class);
        Map<?, ?> device = (Map<?, ?>) ((List<?>) page.get("items")).getFirst();
        // La página expone únicamente los cinco campos del contrato.
        assertThat(page.keySet()).isEqualTo(Set.of("items", "page", "size", "totalElements", "totalPages"));
        // El DTO contiene doce campos, incluidos los dos nullable, sin entidades ni secretos.
        assertThat(device.keySet()).isEqualTo(Set.of("id", "deviceIdentifier", "name", "companyId", "userId",
                "licenseId", "policyId", "enrollmentTokenId", "active", "createdAt", "updatedAt", "lastSeenAt"));
        list(fixture.admin(), "?size=2").andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].id").value(fixture.first()))
                .andExpect(jsonPath("$.items[1].id").value(fixture.anotherDevice()))
                .andExpect(jsonPath("$.totalElements").value(4)).andExpect(jsonPath("$.totalPages").value(2));
        list(fixture.admin(), "?size=2&page=1").andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].id").value(fixture.inactive()))
                .andExpect(jsonPath("$.items[1].id").value(fixture.adminDevice()));
        list(fixture.admin(), "?size=2&page=8").andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalElements").value(4));
        list(fixture.foreign(), "").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1))
                .andExpect(jsonPath("$.items[0].id").value(fixture.foreignDevice()));
    }

    @Test
    void appliesFiltersAndDoesNotRecordActivityOnLists() throws Exception {
        Map<String, Object> before = stored(fixture.first());
        list(fixture.admin(), "?active=true").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(3));
        list(fixture.owner(), "?active=true").andExpect(status().isOk()).andExpect(jsonPath("$.totalElements").value(1))
                .andExpect(jsonPath("$.items[0].id").value(fixture.first()));
        list(fixture.owner(), "?active=false&userId=" + fixture.owner().userId()).andExpect(status().isOk())
                .andExpect(jsonPath("$.totalElements").value(1)).andExpect(jsonPath("$.items[0].id").value(fixture.inactive()));
        list(fixture.admin(), "?userId=" + fixture.another().userId()).andExpect(status().isOk())
                .andExpect(jsonPath("$.totalElements").value(1)).andExpect(jsonPath("$.items[0].id").value(fixture.anotherDevice()));
        list(fixture.admin(), "?userId=" + fixture.admin().userId() + "&active=false").andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalElements").value(0))
                .andExpect(jsonPath("$.totalPages").value(0));
        // Consultar páginas y filtros conserva lastSeenAt y la auditoría del dispositivo.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    @Test
    void userSeesAdminPolicyChangesAndRecordsServerActivity() throws Exception {
        Map<String, Object> before = stored(fixture.first());
        Map<String, Object> otherBefore = stored(fixture.anotherDevice());
        mvc.perform(patch("/api/v1/policies/" + fixture.policy()).header("Authorization", bearer(fixture.admin()))
                .contentType(MediaType.APPLICATION_JSON).content(
                        "{\"name\":\"Actualizada\",\"urlRules\":[\"NEW.EXAMPLE.\"],\"downloadRules\":[\".PDF\"]}"))
                .andExpect(status().isOk());
        String json = policy(fixture.owner(), fixture.first()).andExpect(status().isOk())
                .andExpect(jsonPath("$.name").value("Actualizada")).andExpect(jsonPath("$.urlRules[0]").value("new.example"))
                .andExpect(jsonPath("$.downloadRules[0]").value("pdf")).andExpect(jsonPath("$.updatedAt").value(NOW.toString()))
                .andReturn().getResponse().getContentAsString();
        // La respuesta reutiliza exactamente PolicyResponse, sin mezclar campos de instalación.
        assertThat(mapper.readValue(json, PolicyResponse.class).id()).isEqualTo(fixture.policy());
        Map<String, Object> after = stored(fixture.first());
        // La actividad usa el Clock del servidor y se confirma antes de responder.
        assertThat(((java.sql.Timestamp) after.get("last_seen_at")).toInstant()).isEqualTo(NOW);
        // Se conserva el instante de creación.
        assertThat(after.get("created_at")).isEqualTo(before.get("created_at"));
        // Las referencias históricas permanecen intactas.
        assertThat(after).containsEntry("enrollment_token_id", before.get("enrollment_token_id"))
                .containsEntry("license_id", before.get("license_id")).containsEntry("user_id", before.get("user_id"));
        // La comunicación de una instalación no modifica la de otro propietario.
        assertThat(stored(fixture.anotherDevice())).isEqualTo(otherBefore);
    }

    @Test
    void adminMustAlsoOwnInstallationAndInactiveCheckDoesNotLeakExistence() throws Exception {
        Map<String, Object> firstBefore = stored(fixture.first());
        Map<String, Object> inactiveBefore = stored(fixture.inactive());
        for (Long id : List.of(fixture.anotherDevice(), fixture.foreignDevice(), Long.MAX_VALUE)) {
            policy(fixture.owner(), id).andExpect(status().isNotFound()).andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
        }
        policy(fixture.admin(), fixture.first()).andExpect(status().isNotFound());
        policy(fixture.admin(), fixture.inactive()).andExpect(status().isNotFound());
        policy(fixture.owner(), fixture.inactive()).andExpect(status().isConflict())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON))
                .andExpect(jsonPath("$.code").value("DEVICE_INACTIVE"));
        // Los rechazos de propiedad no registran actividad ni cambian el dispositivo.
        assertThat(stored(fixture.first())).isEqualTo(firstBefore);
        // El rechazo de una instalación propia inactiva conserva también toda su información.
        assertThat(stored(fixture.inactive())).isEqualTo(inactiveBefore);
        policy(fixture.admin(), fixture.adminDevice()).andExpect(status().isOk());
    }

    @Test
    void resolvesUserFilterOnlyWithinPermittedScope() throws Exception {
        for (Long userId : List.of(fixture.another().userId(), fixture.foreign().userId(), Long.MAX_VALUE)) {
            list(fixture.owner(), "?userId=" + userId).andExpect(status().isNotFound())
                    .andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
        }
        for (Long userId : List.of(fixture.foreign().userId(), Long.MAX_VALUE)) {
            list(fixture.admin(), "?userId=" + userId).andExpect(status().isNotFound())
                    .andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
        }
    }

    @ParameterizedTest
    @ValueSource(strings = {"page=-1", "size=0", "size=101", "page=no", "size=1.5", "page=2147483648",
            "userId=0", "userId=-1", "userId=text", "userId=9223372036854775808", "userId=",
            "active=1", "active=TRUE", "active=", "sort=id", "companyId=2", "page=0&page=1",
            "active=true&active=false", "userId=1&userId=2"})
    void rejectsInvalidListParametersWithoutActivity(String query) throws Exception {
        Map<String, Object> before = stored(fixture.first());
        list(fixture.admin(), "?" + query).andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
        // La validación de parámetros se completa sin modificar la instalación.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "-1", "invalid", "9223372036854775808"})
    void rejectsInvalidDeviceIds(String id) throws Exception {
        mvc.perform(get(BASE + "/" + id + "/policy").header("Authorization", bearer(fixture.owner())))
                .andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
    }

    @Test
    void keepsOtherDeviceEndpointsClosedAndRequiresBearer() throws Exception {
        mvc.perform(get(BASE)).andExpect(status().isUnauthorized())
                .andExpect(header().string("WWW-Authenticate", "Bearer realm=\"sbp\""));
        mvc.perform(get(BASE + "/" + fixture.first() + "/policy")).andExpect(status().isUnauthorized());
        String jwt = bearer(fixture.admin());
        mvc.perform(get(BASE + "/" + fixture.first()).header("Authorization", jwt)).andExpect(status().isForbidden());
        mvc.perform(post(BASE).header("Authorization", jwt)).andExpect(status().isForbidden());
        mvc.perform(patch(BASE + "/" + fixture.first()).header("Authorization", jwt)
                .contentType(MediaType.APPLICATION_JSON).content("{\"active\":false}")).andExpect(status().isForbidden());
        mvc.perform(delete(BASE + "/" + fixture.first()).header("Authorization", jwt)).andExpect(status().isForbidden());
        Map<String, Object> before = stored(fixture.first());
        mvc.perform(get(BASE + "/" + fixture.first() + "/policy?userId=1")
                .header("Authorization", bearer(fixture.owner()))).andExpect(status().isBadRequest());
        // Un query ajeno al contrato no puede registrar actividad ni sustituir el propietario.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    @Test
    void rollsBackLastSeenAfterFlushedFailure() throws Exception {
        Map<String, Object> before = stored(fixture.first());
        AtomicBoolean flushed = new AtomicBoolean();
        Advised proxy = (Advised) repository;
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
            policy(fixture.owner(), fixture.first()).andExpect(status().isInternalServerError())
                    .andExpect(jsonPath("$.code").value("INTERNAL_ERROR"));
        } finally {
            proxy.removeAdvice(fail);
        }
        // La prueba provoca el fallo después del flush real, no antes de ejecutar SQL.
        assertThat(flushed).isTrue();
        // El rollback conserva lastSeenAt y updatedAt de la instalación.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    @Test
    void boundsConcurrentActivityWithNowaitAndAllowsRetry() throws Exception {
        Map<String, Object> before = stored(fixture.first());
        CountDownLatch locked = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        try (var executor = Executors.newSingleThreadExecutor()) {
            var writer = executor.submit(() -> tx().executeWithoutResult(status -> {
                repository.findByIdAndCompany_IdAndUser_Id(fixture.first(),
                        fixture.owner().companyId(), fixture.owner().userId()).orElseThrow();
                locked.countDown();
                await(release);
            }));
            try {
                // La otra transacción ya posee el bloqueo de la instalación.
                assertThat(locked.await(10, TimeUnit.SECONDS)).isTrue();
                policy(fixture.owner(), fixture.first()).andExpect(status().isServiceUnavailable())
                        .andExpect(jsonPath("$.code").value("SERVICE_UNAVAILABLE"));
                // Un rechazo por contención no confirma actividad parcial.
                assertThat(stored(fixture.first())).isEqualTo(before);
            } finally {
                release.countDown();
                writer.get(10, TimeUnit.SECONDS);
            }
        }
        policy(fixture.owner(), fixture.first()).andExpect(status().isOk());
    }

    @Test
    void policySnapshotRemainsCoherentDuringConcurrentAdminEdit() throws Exception {
        PolicyResponse before = policies.get(fixture.admin(), fixture.policy());
        CountDownLatch loaded = new CountDownLatch(1);
        CountDownLatch edited = new CountDownLatch(1);
        AtomicBoolean first = new AtomicBoolean(true);
        Advised proxy = (Advised) repository;
        MethodInterceptor pause = invocation -> {
            Object result = invocation.proceed();
            if (invocation.getMethod().getName().equals("findByIdAndCompany_IdAndUser_Id")
                    && first.compareAndSet(true, false)) {
                loaded.countDown();
                await(edited);
            }
            return result;
        };
        proxy.addAdvice(0, pause);
        try (var executor = Executors.newSingleThreadExecutor()) {
            var reader = executor.submit(() -> service.getPolicy(fixture.owner(), fixture.first()));
            try {
                // La instalación ya se ha resuelto, antes de inicializar Policy y sus colecciones.
                assertThat(loaded.await(10, TimeUnit.SECONDS)).isTrue();
                policies.update(fixture.admin(), fixture.policy(), mapper.readValue(
                        "{\"name\":\"Concurrente\",\"urlRules\":[\"new.example\"],\"downloadRules\":[\"pdf\"]}",
                        PolicyUpdateRequest.class));
            } finally {
                edited.countDown();
            }
            // El lector devuelve la versión anterior completa, con su configuración, reglas y fechas.
            assertThat(reader.get(10, TimeUnit.SECONDS)).isEqualTo(before);
        } finally {
            proxy.removeAdvice(pause);
        }
        // La siguiente comunicación obtiene la versión nueva completa.
        assertThat(service.getPolicy(fixture.owner(), fixture.first()).urlRules()).containsExactly("new.example");
    }

    private User user(Company company, UserRole role) {
        User user = new User("Ana", "Demo", UUID.randomUUID() + "@example.com", HASH, role, company);
        em.persist(user);
        return user;
    }

    private AuthenticatedUser actor(User user) {
        return new AuthenticatedUser(user.getId(), user.getCompany().getId(), user.getRole());
    }

    private Device device(Company company, User owner, License license, Policy policy, boolean active) {
        EnrollmentToken enrollment = new EnrollmentToken(license, owner, policy,
                UUID.randomUUID().toString().replace("-", "").repeat(2), INITIAL, INITIAL.plusSeconds(3600));
        enrollment.markUsed(INITIAL);
        em.persist(enrollment);
        Device device = new Device(UUID.randomUUID().toString(), null, company, owner, license, policy, enrollment);
        if (!active) device.deactivate();
        em.persist(device);
        return device;
    }

    private String bearer(AuthenticatedUser actor) {
        return "Bearer " + tokens.issue(actor.userId(), clock.instant().plusSeconds(604800)).getTokenValue();
    }

    private ResultActions list(AuthenticatedUser actor, String query) throws Exception {
        return mvc.perform(get(BASE + query).header("Authorization", bearer(actor)));
    }

    private ResultActions policy(AuthenticatedUser actor, Long id) throws Exception {
        return mvc.perform(get(BASE + "/" + id + "/policy").header("Authorization", bearer(actor)));
    }

    private Map<String, Object> stored(Long id) {
        return jdbc.queryForMap("select * from devices where id=?", id);
    }

    private TransactionTemplate tx() {
        TransactionTemplate template = new TransactionTemplate(transactionManager);
        template.setTimeout(10);
        return template;
    }

    private static void await(CountDownLatch latch) {
        try {
            if (!latch.await(10, TimeUnit.SECONDS)) throw new IllegalStateException("Test coordination timed out");
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Test interrupted", exception);
        }
    }

    private record Fixture(AuthenticatedUser admin, AuthenticatedUser owner, AuthenticatedUser another,
            AuthenticatedUser foreign, Long first, Long inactive, Long anotherDevice, Long foreignDevice,
            Long adminDevice, Long policy) {}

    @TestConfiguration(proxyBeanMethods = false)
    static class FixedClockConfiguration {
        @Bean
        @Primary
        Clock deviceTestClock() { return Clock.fixed(NOW, ZoneOffset.UTC); }
    }
}
