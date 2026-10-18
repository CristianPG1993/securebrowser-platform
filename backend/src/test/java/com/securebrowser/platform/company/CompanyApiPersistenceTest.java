package com.securebrowser.platform.company;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Stream;

import com.nimbusds.jose.JOSEException;
import com.nimbusds.jose.JOSEObjectType;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSHeader;
import com.nimbusds.jose.JWSObject;
import com.nimbusds.jose.Payload;
import com.nimbusds.jose.crypto.MACSigner;
import com.securebrowser.platform.security.JwtProperties;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.context.annotation.Import;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.test.json.JsonCompareMode;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Recorre la API real de Company con JWT firmados y PostgreSQL temporal. */
@SpringBootTest(properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-company-api-test",
        "sbp.security.jwt.audience=sbp-company-api",
        "sbp.security.jwt.max-lifetime-seconds=900"
})
@AutoConfigureMockMvc
@Import(CompanyApiTestConfiguration.class)
class CompanyApiPersistenceTest {

    private static final String COMPANY_PATH = "/api/v1/companies/me";

    // Fecha reproducible de las filas, independiente del reloj que valida los JWT.
    private static final Instant INITIAL_TIME = Instant.parse("2026-10-18T08:00:00Z");

    // Hash público utilizado únicamente para crear usuarios de prueba.
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Envía peticiones por los filtros y el Controller reales.
    @Autowired
    private MockMvc mvc;

    // Prepara y consulta datos sin sustituir Repository ni Service por mocks.
    @PersistenceContext
    private EntityManager entityManager;

    @Autowired
    private PlatformTransactionManager transactionManager;

    // Usa la configuración del contexto de prueba para firmar tokens compatibles.
    @Autowired
    private JwtProperties jwtProperties;

    @Autowired
    private Clock clock;

    // Controla las fechas de auditoría para poder comprobarlas exactamente.
    @Autowired
    private AuditingHandler auditingHandler;

    // Limita la limpieza a las compañías creadas por esta prueba.
    private final List<Long> companyIds = new ArrayList<>();

    /** Fija las fechas de creación sin cambiar el reloj de autenticación. */
    @BeforeEach
    void setUp() {
        auditingHandler.setDateTimeProvider(() -> Optional.of(INITIAL_TIME));
    }

    /** Cada ADMIN obtiene exclusivamente los cuatro campos públicos de su compañía. */
    @Test
    void returnsOnlyOwnCompanyForEachAdmin() throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);

        assertThat(adminA.companyId()).isNotEqualTo(adminB.companyId());
        assertOwnCompanyResponse(adminA);
        assertOwnCompanyResponse(adminB);
    }

    /** El PATCH confirma el nombre y la auditoría sin modificar la otra compañía. */
    @Test
    void commitsNameChangeOnlyForOwnCompany() throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);
        Company beforeB = readStoredCompany(adminB.companyId());
        Instant modifiedAt = INITIAL_TIME.plusSeconds(60);
        auditingHandler.setDateTimeProvider(() -> Optional.of(modifiedAt));

        mvc.perform(patch(COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token(adminA.userId()))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "  Empresa A renovada  "}
                                """))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(content().json("""
                        {
                            "id": %d,
                            "name": "Empresa A renovada",
                            "createdAt": "2026-10-18T08:00:00Z",
                            "updatedAt": "2026-10-18T08:01:00Z"
                        }
                        """.formatted(adminA.companyId()), JsonCompareMode.STRICT));

        Company storedA = readStoredCompany(adminA.companyId());
        Company storedB = readStoredCompany(adminB.companyId());

        assertThat(storedA.getId()).isEqualTo(adminA.companyId());
        assertThat(storedA.getName()).isEqualTo("Empresa A renovada");
        assertThat(storedA.getCreatedAt()).isEqualTo(INITIAL_TIME);
        assertThat(storedA.getUpdatedAt()).isEqualTo(modifiedAt);

        assertThat(storedB.getId()).isEqualTo(beforeB.getId());
        assertThat(storedB.getName()).isEqualTo(beforeB.getName());
        assertThat(storedB.getCreatedAt()).isEqualTo(beforeB.getCreatedAt());
        assertThat(storedB.getUpdatedAt()).isEqualTo(beforeB.getUpdatedAt());
    }

    /** Un PATCH incorrecto no modifica ninguna de las compañías ni sus fechas. */
    @ParameterizedTest
    @MethodSource("invalidPatchBodies")
    void rejectsInvalidPatchWithoutChangingStoredCompanies(String body) throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);
        Company beforeA = readStoredCompany(adminA.companyId());
        Company beforeB = readStoredCompany(adminB.companyId());
        auditingHandler.setDateTimeProvider(() -> Optional.of(INITIAL_TIME.plusSeconds(60)));

        mvc.perform(patch(COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token(adminA.userId()))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content(body))
                .andExpect(status().isBadRequest());

        assertStoredCompanyUnchanged(beforeA);
        assertStoredCompanyUnchanged(beforeB);
    }

    /** Los USER de ambas compañías no pueden consultarlas ni modificarlas. */
    @Test
    void rejectsUserAccessWithoutChangingEitherCompany() throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);
        Long userA = persistUserInCompany(adminA.companyId(), UserRole.USER);
        Long userB = persistUserInCompany(adminB.companyId(), UserRole.USER);
        Company beforeA = readStoredCompany(adminA.companyId());
        Company beforeB = readStoredCompany(adminB.companyId());
        auditingHandler.setDateTimeProvider(() -> Optional.of(INITIAL_TIME.plusSeconds(60)));

        for (Long userId : List.of(userA, userB)) {
            String authorization = "Bearer " + token(userId);

            mvc.perform(get(COMPANY_PATH)
                            .header(HttpHeaders.AUTHORIZATION, authorization)
                            .accept(MediaType.APPLICATION_JSON))
                    .andExpect(status().isForbidden());

            mvc.perform(patch(COMPANY_PATH)
                            .header(HttpHeaders.AUTHORIZATION, authorization)
                            .contentType(MediaType.APPLICATION_JSON)
                            .accept(MediaType.APPLICATION_JSON)
                            .content("""
                                    {"name": "Cambio sin permiso"}
                                    """))
                    .andExpect(status().isForbidden());

            assertStoredCompanyUnchanged(beforeA);
            assertStoredCompanyUnchanged(beforeB);
        }
    }

    /** Sin Bearer se rechazan consulta y cambio, conservando ambas compañías. */
    @Test
    void rejectsMissingAuthenticationWithoutChangingEitherCompany() throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);
        Company beforeA = readStoredCompany(adminA.companyId());
        Company beforeB = readStoredCompany(adminB.companyId());
        auditingHandler.setDateTimeProvider(() -> Optional.of(INITIAL_TIME.plusSeconds(60)));

        mvc.perform(get(COMPANY_PATH).accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isUnauthorized())
                .andExpect(header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer"));

        mvc.perform(patch(COMPANY_PATH)
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "Cambio sin autenticación"}
                                """))
                .andExpect(status().isUnauthorized());

        assertStoredCompanyUnchanged(beforeA);
        assertStoredCompanyUnchanged(beforeB);
    }

    /** Un JWT bien formado con firma incorrecta no permite leer ni modificar datos. */
    @Test
    void rejectsInvalidSignatureWithoutChangingEitherCompany() throws Exception {
        TestActor adminA = persistActor("Empresa A", UserRole.ADMIN);
        TestActor adminB = persistActor("Empresa B", UserRole.ADMIN);
        Company beforeA = readStoredCompany(adminA.companyId());
        Company beforeB = readStoredCompany(adminB.companyId());
        auditingHandler.setDateTimeProvider(() -> Optional.of(INITIAL_TIME.plusSeconds(60)));

        // La clave válida de este contexto contiene 32 ceros; cambiamos un byte.
        byte[] wrongKey = new byte[32];
        wrongKey[0] = 1;
        String authorization = "Bearer " + token(adminA.userId(), wrongKey);

        mvc.perform(get(COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, authorization)
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isUnauthorized())
                .andExpect(header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer error=\"invalid_token\""));

        mvc.perform(patch(COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, authorization)
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "Cambio con firma incorrecta"}
                                """))
                .andExpect(status().isUnauthorized());

        assertStoredCompanyUnchanged(beforeA);
        assertStoredCompanyUnchanged(beforeB);
    }

    /** Combina errores de validación, lectura de JSON y campos que no se pueden modificar. */
    private static Stream<String> invalidPatchBodies() {
        return Stream.of(
                "{}",
                "{\"name\": null}",
                "{\"name\": \"   \"}",
                "{\"name\": \"%s\"}".formatted("a".repeat(151)),
                "{\"name\": 123}",
                "{\"name\":",
                "{\"name\": \"Empresa alterada\", \"id\": 999}");
    }

    /** Vuelve a consultar PostgreSQL y compara todos los campos públicos de la compañía. */
    private void assertStoredCompanyUnchanged(Company before) {
        Company stored = readStoredCompany(before.getId());
        assertThat(stored.getId()).isEqualTo(before.getId());
        assertThat(stored.getName()).isEqualTo(before.getName());
        assertThat(stored.getCreatedAt()).isEqualTo(before.getCreatedAt());
        assertThat(stored.getUpdatedAt()).isEqualTo(before.getUpdatedAt());
    }

    /** Consulta en otra transacción para comprobar lo confirmado por la petición HTTP. */
    private Company readStoredCompany(Long companyId) {
        return Objects.requireNonNull(new TransactionTemplate(transactionManager).execute(status ->
                entityManager.find(Company.class, companyId)));
    }

    /** Consulta usando Authorization: Bearer y comprueba el JSON completo. */
    private void assertOwnCompanyResponse(TestActor actor) throws Exception {
        var result = mvc.perform(get(COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token(actor.userId()))
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(content().json("""
                        {
                            "id": %d,
                            "name": "%s",
                            "createdAt": "2026-10-18T08:00:00Z",
                            "updatedAt": "2026-10-18T08:00:00Z"
                        }
                        """.formatted(actor.companyId(), actor.companyName()), JsonCompareMode.STRICT))
                .andReturn();

        assertThat(result.getRequest().getSession(false)).isNull();
    }

    /** Confirma los datos antes de que la petición abra sus propias transacciones. */
    private TestActor persistActor(String companyName, UserRole role) {
        return Objects.requireNonNull(new TransactionTemplate(transactionManager).execute(status -> {
            Company company = new Company(companyName);
            entityManager.persist(company);
            companyIds.add(company.getId());

            User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                    PASSWORD_HASH, role, company);
            entityManager.persist(user);
            entityManager.flush();

            return new TestActor(user.getId(), company.getId(), company.getName());
        }));
    }

    /** Añade un usuario a una compañía existente y confirma sus datos antes del HTTP. */
    private Long persistUserInCompany(Long companyId, UserRole role) {
        return Objects.requireNonNull(new TransactionTemplate(transactionManager).execute(status -> {
            Company company = Objects.requireNonNull(entityManager.find(Company.class, companyId));
            User user = new User("Luis", "Pérez", UUID.randomUUID() + "@example.com",
                    PASSWORD_HASH, role, company);
            entityManager.persist(user);
            entityManager.flush();
            return user.getId();
        }));
    }

    /** Firma un JWT vigente; el backend resolverá compañía y rol desde PostgreSQL. */
    private String token(Long userId) throws JOSEException {
        return token(userId, jwtProperties.getSecretKey().getEncoded());
    }

    /** Permite firmar con otra clave para comprobar el rechazo criptográfico real. */
    private String token(Long userId, byte[] signingKey) throws JOSEException {
        Instant now = clock.instant();
        Map<String, Object> claims = Map.of(
                "sub", userId.toString(),
                "iss", jwtProperties.getIssuer(),
                "aud", List.of(jwtProperties.getAudience()),
                "iat", now.minusSeconds(30).getEpochSecond(),
                "exp", now.plusSeconds(300).getEpochSecond(),
                "jti", UUID.randomUUID().toString());

        JWSObject jwt = new JWSObject(new JWSHeader.Builder(JWSAlgorithm.HS256)
                .type(JOSEObjectType.JWT).build(), new Payload(claims));
        jwt.sign(new MACSigner(signingKey));
        return jwt.serialize();
    }

    /** Restaura la auditoría y elimina únicamente los datos confirmados de esta prueba. */
    @AfterEach
    void cleanUp() {
        auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        SecurityContextHolder.clearContext();

        if (companyIds.isEmpty()) {
            return;
        }

        new TransactionTemplate(transactionManager).executeWithoutResult(status -> {
            for (Long companyId : companyIds) {
                entityManager.createNativeQuery("DELETE FROM users WHERE company_id = :companyId")
                        .setParameter("companyId", companyId).executeUpdate();
                entityManager.createNativeQuery("DELETE FROM companies WHERE id = :companyId")
                        .setParameter("companyId", companyId).executeUpdate();
            }
        });
        companyIds.clear();
    }

    /** Conserva los identificadores reales y el nombre esperado de los datos de prueba. */
    private record TestActor(Long userId, Long companyId, String companyName) {
    }
}
