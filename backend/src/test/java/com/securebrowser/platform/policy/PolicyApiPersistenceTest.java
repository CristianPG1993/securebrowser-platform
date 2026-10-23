package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.*;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.*;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.*;

import java.time.Clock;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.stream.Stream;

import com.securebrowser.platform.auth.AccessTokenService;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.policy.dto.PolicyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;
import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;
import org.junit.jupiter.api.*;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.*;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.aop.framework.Advised;
import org.aopalliance.intercept.MethodInterceptor;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.context.annotation.Import;
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

/** Recorre HTTP, autorización y cambios confirmados con dos Companies y PostgreSQL temporal. */
@SpringBootTest(properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-policy-test", "sbp.security.jwt.audience=sbp-policy-api",
        "sbp.security.jwt.max-lifetime-seconds=900"
})
@AutoConfigureMockMvc
@Import(PostgresRepositoryTestConfiguration.class)
class PolicyApiPersistenceTest {
    private static final String BASE = "/api/v1/policies";
    private static final Instant INITIAL = Instant.parse("2026-10-23T08:00:00Z");
    private static final Instant MODIFIED = INITIAL.plusSeconds(60);
    private static final String HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    @Autowired private MockMvc mvc;
    @Autowired private JsonMapper mapper;
    @Autowired private PolicyService service;
    @Autowired private PolicyRepository repository;
    @Autowired private AccessTokenService tokens;
    @Autowired private Clock clock;
    @Autowired private JdbcTemplate jdbc;
    @Autowired private AuditingHandler auditing;
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
            AuthenticatedUser adminA = actor(a, UserRole.ADMIN);
            AuthenticatedUser userA = actor(a, UserRole.USER);
            AuthenticatedUser adminB = actor(b, UserRole.ADMIN);
            Policy first = new Policy("Primera", a);
            first.addUrlRule("keep.example");
            first.addUrlRule("old.example");
            first.addDownloadRule("exe");
            first.addDownloadRule("pdf");
            em.persist(first);
            Policy second = new Policy("Segunda", a);
            Policy third = new Policy("Tercera", a);
            Policy other = new Policy("Privada de B", b);
            em.persist(second);
            em.persist(other);
            em.persist(third);
            em.flush();
            return new Fixture(adminA, userA, adminB, first.getId(), second.getId(), third.getId(), other.getId());
        });
        auditing.setDateTimeProvider(() -> Optional.of(MODIFIED));
    }

    @AfterEach
    void cleanup() {
        auditing.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        if (fixture != null) {
            for (Long company : List.of(fixture.adminA().companyId(), fixture.adminB().companyId())) {
                jdbc.update("delete from policies where company_id = ?", company);
                jdbc.update("delete from users where company_id = ?", company);
                jdbc.update("delete from companies where id = ?", company);
            }
        }
    }

    @Test
    void listsOnlyOwnPoliciesWithStablePaginationAndEmptyPages() throws Exception {
        readList("?size=2", fixture.adminA()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items.length()").value(2))
                .andExpect(jsonPath("$.items[0].id").value(fixture.first()))
                .andExpect(jsonPath("$.items[1].id").value(fixture.second()))
                .andExpect(jsonPath("$.page").value(0)).andExpect(jsonPath("$.size").value(2))
                .andExpect(jsonPath("$.totalElements").value(3)).andExpect(jsonPath("$.totalPages").value(2));
        readList("?size=2&page=1", fixture.adminA()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items[0].id").value(fixture.third()));
        readList("?size=2&page=9", fixture.adminA()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalElements").value(3));
        readList("", fixture.adminB()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items.length()").value(1))
                .andExpect(jsonPath("$.items[0].id").value(fixture.other()))
                .andExpect(jsonPath("$.size").value(20)).andExpect(jsonPath("$.totalElements").value(1));
        jdbc.update("delete from policies where company_id = ?", fixture.adminA().companyId());
        readList("", fixture.adminA()).andExpect(status().isOk())
                .andExpect(jsonPath("$.items").isEmpty()).andExpect(jsonPath("$.totalElements").value(0))
                .andExpect(jsonPath("$.totalPages").value(0));
    }

    @Test
    void exposesOnlyContractFieldsAndNormalizedRules() throws Exception {
        String json = mvc.perform(get(BASE + "/" + fixture.first()).header("Authorization", bearer(fixture.adminA())))
                .andExpect(status().isOk()).andExpect(jsonPath("$.companyId").value(fixture.adminA().companyId()))
                .andExpect(jsonPath("$.urlRules[0]").value("keep.example")).andReturn().getResponse().getContentAsString();
        Map<?, ?> body = mapper.readValue(json, Map.class);
        // El snapshot no publica entidades, IDs internos de reglas ni campos adicionales.
        assertThat(body.keySet()).isEqualTo(Set.of("id", "companyId", "name", "urlFilteringEnabled",
                "urlFilteringMode", "urlRules", "downloadControlEnabled", "downloadControlMode",
                "downloadRules", "createdAt", "updatedAt"));
    }

    @Test
    void patchesAllFieldsAtomicallyAndPreservesOtherPolicies() throws Exception {
        PolicyResponse otherBefore = stored(fixture.other());
        PolicyResponse secondBefore = stored(fixture.second());
        update(fixture.first(), """
                {"name":"  Renovada  ","urlFilteringEnabled":false,"urlFilteringMode":"ALLOWLIST",
                 "urlRules":[" BÜCHER.Example. ","KEEP.EXAMPLE."],
                 "downloadControlEnabled":false,"downloadControlMode":"ALLOWLIST","downloadRules":[".PDF","ZIP"]}
                """).andExpect(status().isOk()).andExpect(jsonPath("$.name").value("Renovada"))
                .andExpect(jsonPath("$.urlFilteringEnabled").value(false))
                .andExpect(jsonPath("$.urlFilteringMode").value("ALLOWLIST"))
                .andExpect(jsonPath("$.urlRules[1]").value("xn--bcher-kva.example"))
                .andExpect(jsonPath("$.downloadControlEnabled").value(false))
                .andExpect(jsonPath("$.downloadControlMode").value("ALLOWLIST"))
                .andExpect(jsonPath("$.downloadRules[0]").value("pdf"))
                .andExpect(jsonPath("$.createdAt").value(INITIAL.toString()))
                .andExpect(jsonPath("$.updatedAt").value(MODIFIED.toString()));
        // La lectura desde otra transacción confirma el nombre y las listas nuevas.
        assertThat(stored(fixture.first()).urlRules()).containsExactly("keep.example", "xn--bcher-kva.example");
        // La modificación no afecta a otra compañía.
        assertThat(stored(fixture.other())).isEqualTo(otherBefore);
        // Tampoco altera otra política de la misma compañía.
        assertThat(stored(fixture.second())).isEqualTo(secondBefore);
    }

    @Test
    void preservesOmittedFieldsAndCanClearEachListIndependently() throws Exception {
        PolicyResponse before = stored(fixture.first());
        update(fixture.first(), "{\"urlFilteringEnabled\":false}").andExpect(status().isOk())
                .andExpect(jsonPath("$.urlFilteringEnabled").value(false));
        PolicyResponse changed = stored(fixture.first());
        // Un false presente no elimina la lista omitida.
        assertThat(changed.urlRules()).isEqualTo(before.urlRules());
        // El nombre omitido se conserva.
        assertThat(changed.name()).isEqualTo(before.name());
        update(fixture.first(), "{\"urlRules\":[]}").andExpect(status().isOk())
                .andExpect(jsonPath("$.urlRules").isEmpty());
        // Vaciar URLs conserva las extensiones omitidas.
        assertThat(stored(fixture.first()).downloadRules()).isEqualTo(before.downloadRules());
        update(fixture.first(), "{\"downloadRules\":[]}").andExpect(status().isOk())
                .andExpect(jsonPath("$.downloadRules").isEmpty());
    }

    @Test
    void replacesOverlappingListsWithoutDuplicateInsertsAndAcceptsNormalizedNoOp() throws Exception {
        Long keptId = jdbc.queryForObject("select id from url_rules where policy_id=? and domain='keep.example'",
                Long.class, fixture.first());
        String patch = "{\"urlRules\":[\"KEEP.EXAMPLE.\",\"new.example\"],\"downloadRules\":[\".PDF\",\"zip\"]}";
        update(fixture.first(), patch).andExpect(status().isOk());
        update(fixture.first(), patch).andExpect(status().isOk());
        // El resultado contiene exactamente el reemplazo, sin las reglas antiguas.
        assertThat(stored(fixture.first()).urlRules()).containsExactly("keep.example", "new.example");
        // La fila común se conserva y no se borra para insertarla de nuevo.
        assertThat(jdbc.queryForObject("select id from url_rules where policy_id=? and domain='keep.example'",
                Long.class, fixture.first())).isEqualTo(keptId);
    }

    @ParameterizedTest
    @MethodSource("invalidPatches")
    void rejectsInvalidPatchesWithoutChangingStoredState(String body) throws Exception {
        PolicyResponse before = stored(fixture.first());
        update(fixture.first(), body).andExpect(status().isBadRequest())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON))
                .andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
        // Se conserva todo el snapshot, incluidas ambas listas y sus fechas.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    static Stream<String> invalidPatches() {
        return Stream.of("", "null", "[]", "{}", "{", "{\"name\":null}", "{\"name\":2}",
                "{\"name\":\"\"}", "{\"name\":\"   \"}", "{\"name\":\"" + "a".repeat(151) + "\"}",
                "{\"companyId\":1}", "{\"id\":1}", "{\"updatedAt\":\"2026-10-23T00:00:00Z\"}",
                "{\"urlFilteringEnabled\":\"false\"}", "{\"urlFilteringEnabled\":0}",
                "{\"downloadControlEnabled\":null}", "{\"downloadControlEnabled\":1}",
                "{\"urlFilteringMode\":\"allowlist\"}", "{\"urlFilteringMode\":1}",
                "{\"downloadControlMode\":\"INVALID\"}", "{\"urlRules\":null}", "{\"urlRules\":[null]}",
                "{\"urlRules\":[1]}", "{\"urlRules\":\"example.com\"}",
                "{\"urlRules\":[\"https://example.com\"]}", "{\"urlRules\":[\"example.com\",\"EXAMPLE.COM.\"]}",
                "{\"downloadRules\":[true]}", "{\"downloadRules\":null}", "{\"downloadRules\":[\"tar.gz\"]}",
                "{\"downloadRules\":[\".EXE\",\"exe\"]}", "{\"downloadRules\":[\" pdf\"]}",
                "{\"name\":\"Cambiada\",\"urlRules\":[\"ok.example\"],\"downloadRules\":[\"bad/path\"]}",
                "{\"name\":\"Cambiada\"} {}");
    }

    @ParameterizedTest
    @ValueSource(strings = {"page=-1", "size=0", "size=101", "page=abc", "size=1.0",
            "size=", "sort=name", "companyId=2", "page=0&page=1", "page=2147483648"})
    void rejectsInvalidPaginationAndUnknownQuery(String query) throws Exception {
        readList("?" + query, fixture.adminA()).andExpect(status().isBadRequest())
                .andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"0", "-1", "abc", "9223372036854775808"})
    void rejectsInvalidIds(String id) throws Exception {
        mvc.perform(get(BASE + "/" + id).header("Authorization", bearer(fixture.adminA())))
                .andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
        mvc.perform(patch(BASE + "/" + id).header("Authorization", bearer(fixture.adminA()))
                .contentType(MediaType.APPLICATION_JSON).content("{\"name\":\"Nueva\"}"))
                .andExpect(status().isBadRequest()).andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
    }

    @Test
    void usesSame404ForForeignAndMissingResourcesAndRejectsUnknownDetailQuery() throws Exception {
        for (Long id : List.of(fixture.other(), Long.MAX_VALUE)) {
            mvc.perform(get(BASE + "/" + id).header("Authorization", bearer(fixture.adminA())))
                    .andExpect(status().isNotFound()).andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
            update(id, "{\"name\":\"Ajena\"}").andExpect(status().isNotFound())
                    .andExpect(jsonPath("$.code").value("RESOURCE_NOT_FOUND"));
        }
        mvc.perform(get(BASE + "/" + fixture.first() + "?companyId=2").header("Authorization", bearer(fixture.adminA())))
                .andExpect(status().isBadRequest());
        mvc.perform(patch(BASE + "/" + fixture.first() + "?name=override")
                .header("Authorization", bearer(fixture.adminA())).contentType(MediaType.APPLICATION_JSON)
                .content("{\"name\":\"Nueva\"}")).andExpect(status().isBadRequest());
    }

    @Test
    void enforcesBearerAndAdminBeforeResolvingResourceOrBody() throws Exception {
        mvc.perform(get(BASE)).andExpect(status().isUnauthorized())
                .andExpect(header().string("WWW-Authenticate", "Bearer realm=\"sbp\""));
        mvc.perform(patch(BASE + "/" + fixture.first()).contentType(MediaType.APPLICATION_JSON).content("{}"))
                .andExpect(status().isUnauthorized());
        readList("", fixture.userA()).andExpect(status().isForbidden());
        for (Long id : List.of(fixture.first(), fixture.other(), Long.MAX_VALUE)) {
            mvc.perform(get(BASE + "/" + id).header("Authorization", bearer(fixture.userA())))
                    .andExpect(status().isForbidden());
            mvc.perform(patch(BASE + "/" + id).header("Authorization", bearer(fixture.userA()))
                    .contentType(MediaType.APPLICATION_JSON).content("{")).andExpect(status().isForbidden());
        }
        mvc.perform(post(BASE).header("Authorization", bearer(fixture.adminA()))
                .contentType(MediaType.APPLICATION_JSON).content("{\"name\":\"Nueva\"}")).andExpect(status().isForbidden());
        mvc.perform(delete(BASE + "/" + fixture.first()).header("Authorization", bearer(fixture.adminA())))
                .andExpect(status().isForbidden());
    }

    @Test
    void rollsBackFlushedConfigurationRulesAndAuditAfterUnexpectedFailure() throws Exception {
        PolicyResponse before = stored(fixture.first());
        AtomicBoolean flushed = new AtomicBoolean();
        MethodInterceptor failureAfterFlush = invocation -> {
            Object result = invocation.proceed();
            if (invocation.getMethod().getName().equals("flush")) {
                flushed.set(true);
                throw new DataIntegrityViolationException("Simulated failure after flushing");
            }
            return result;
        };
        Advised proxy = (Advised) repository;
        proxy.addAdvice(0, failureAfterFlush);
        try {
            update(fixture.first(), "{\"name\":\"No confirmar\",\"urlRules\":[\"new.example\"],\"downloadRules\":[]}")
                    .andExpect(status().isInternalServerError()).andExpect(jsonPath("$.code").value("INTERNAL_ERROR"));
        } finally {
            proxy.removeAdvice(failureAfterFlush);
        }
        // El fallo se provoca después de enviar realmente las escrituras a PostgreSQL.
        assertThat(flushed).isTrue();
        // Aunque las sentencias se enviaron a PostgreSQL, la transacción revierte todas las partes.
        assertThat(stored(fixture.first())).isEqualTo(before);
    }

    @Test
    void lockContentionReturns503WithoutChangesAndAllowsRetry() throws Exception {
        CountDownLatch locked = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        PolicyResponse before = stored(fixture.first());
        try (var executor = Executors.newSingleThreadExecutor()) {
            var owner = executor.submit(() -> tx().executeWithoutResult(status -> {
                repository.findByIdAndCompanyIdForUpdate(fixture.first(), fixture.adminA().companyId()).orElseThrow();
                locked.countDown();
                await(release);
            }));
            try {
                // La segunda conexión compite solo después de adquirir el bloqueo real.
                assertThat(locked.await(10, TimeUnit.SECONDS)).isTrue();
                update(fixture.first(), "{\"name\":\"Reintento\"}").andExpect(status().isServiceUnavailable())
                        .andExpect(jsonPath("$.code").value("SERVICE_UNAVAILABLE"));
                // El rechazo NOWAIT conserva el contenido persistido.
                assertThat(stored(fixture.first())).isEqualTo(before);
            } finally {
                release.countDown();
                owner.get(10, TimeUnit.SECONDS);
            }
        }
        update(fixture.first(), "{\"name\":\"Reintento\"}").andExpect(status().isOk());
        // Al liberar la fila la misma operación puede confirmarse normalmente.
        assertThat(stored(fixture.first()).name()).isEqualTo("Reintento");
    }

    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void readsCoherentSnapshotWhileAnotherTransactionReplacesRules(boolean list) throws Exception {
        PolicyResponse before = stored(fixture.first());
        CountDownLatch loaded = new CountDownLatch(1);
        CountDownLatch written = new CountDownLatch(1);
        AtomicBoolean firstRead = new AtomicBoolean(true);
        String observedMethod = list ? "findByCompany_IdOrderByIdAsc" : "findByIdAndCompany_Id";
        MethodInterceptor pauseAfterRow = invocation -> {
            Object result = invocation.proceed();
            if (invocation.getMethod().getName().equals(observedMethod) && firstRead.compareAndSet(true, false)) {
                loaded.countDown();
                await(written);
            }
            return result;
        };
        Advised proxy = (Advised) repository;
        proxy.addAdvice(0, pauseAfterRow);
        try (var executor = Executors.newSingleThreadExecutor()) {
            var reader = executor.submit(() -> list
                    ? service.list(fixture.adminA(), 0, 20).items().getFirst()
                    : service.get(fixture.adminA(), fixture.first()));
            try {
                // El lector ya dispone de la fila, pero aún no ha recorrido las colecciones lazy.
                boolean reachedRead = loaded.await(10, TimeUnit.SECONDS);
                if (!reachedRead) reader.get(1, TimeUnit.SECONDS);
                assertThat(reachedRead).isTrue();
                service.update(fixture.adminA(), fixture.first(), request(
                        "{\"name\":\"Concurrente\",\"urlRules\":[\"new.example\"],\"downloadRules\":[\"zip\"]}"));
            } finally {
                written.countDown();
            }
            // El lector obtiene fila, reglas y fechas de la misma versión anterior.
            assertThat(reader.get(10, TimeUnit.SECONDS)).isEqualTo(before);
        } finally {
            proxy.removeAdvice(pauseAfterRow);
        }
        // Una lectura posterior ve la versión nueva completa.
        assertThat(stored(fixture.first()).urlRules()).containsExactly("new.example");
    }

    private AuthenticatedUser actor(Company company, UserRole role) {
        User user = new User("Ana", "Demo", UUID.randomUUID() + "@example.com", HASH, role, company);
        em.persist(user);
        return new AuthenticatedUser(user.getId(), company.getId(), role);
    }

    private String bearer(AuthenticatedUser actor) {
        return "Bearer " + tokens.issue(actor.userId(), clock.instant().plusSeconds(604800)).getTokenValue();
    }

    private ResultActions readList(String query, AuthenticatedUser actor) throws Exception {
        return mvc.perform(get(BASE + query).header("Authorization", bearer(actor)));
    }

    private ResultActions update(Long id, String body) throws Exception {
        return mvc.perform(patch(BASE + "/" + id).header("Authorization", bearer(fixture.adminA()))
                .contentType(MediaType.APPLICATION_JSON).content(body));
    }

    private PolicyResponse stored(Long id) {
        return tx().execute(status -> PolicyResponse.from(repository.findById(id).orElseThrow()));
    }

    private PolicyUpdateRequest request(String json) {
        return mapper.readValue(json, PolicyUpdateRequest.class);
    }

    private TransactionTemplate tx() {
        TransactionTemplate transaction = new TransactionTemplate(transactionManager);
        transaction.setTimeout(10);
        return transaction;
    }

    private static void await(CountDownLatch latch) {
        try {
            if (!latch.await(10, TimeUnit.SECONDS)) throw new IllegalStateException("Test coordination timed out");
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Test interrupted", exception);
        }
    }

    private record Fixture(AuthenticatedUser adminA, AuthenticatedUser userA, AuthenticatedUser adminB,
            Long first, Long second, Long third, Long other) {}
}
