package com.securebrowser.platform.security;

import static com.securebrowser.platform.security.SecurityHttpTestSupport.COMPANY_PATH;
import static com.securebrowser.platform.security.SecurityHttpTestSupport.token;
import static com.securebrowser.platform.exception.ApiProblemAssertions.problem;
import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.exception.ApiErrorCode;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;
import jakarta.servlet.DispatcherType;
import jakarta.servlet.Filter;
import jakarta.servlet.RequestDispatcher;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.security.core.userdetails.UserDetailsService;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import org.springframework.web.context.WebApplicationContext;
import org.springframework.web.HttpRequestMethodNotSupportedException;

/** Recorre JWT, filtros y PostgreSQL mediante el Controller real de Company. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.MOCK)
@Import(SecurityHttpTestSupport.ClockConfiguration.class)
class SecurityHttpPersistenceTest {

    // Hash público de prueba: no se utiliza para autenticar estas peticiones Bearer.
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    @Autowired
    private WebApplicationContext context;

    @PersistenceContext
    private EntityManager entityManager;

    @Autowired
    private PlatformTransactionManager transactionManager;

    // Cada prueba elimina únicamente las filas pertenecientes a sus propias compañías.
    private final List<Long> companyIds = new ArrayList<>();
    private MockMvc mvc;

    /** Usa el contexto completo y la misma cadena de filtros que la aplicación. */
    @BeforeEach
    void setUp() {
        mvc = MockMvcBuilders.webAppContextSetup(context)
                .addFilters(context.getBean("springSecurityFilterChain", Filter.class)).build();
    }

    /** El rol persistido determina el acceso a GET y PATCH, sin usuarios en memoria. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void authorizesUsingPersistedRole(UserRole role) throws Exception {
        User user = persistUser(role);
        String bearer = "Bearer " + token(user.getId());
        int expectedStatus = role == UserRole.ADMIN ? 200 : 403;

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().is(expectedStatus))
                .andExpect(result -> {
                    if (role == UserRole.USER) {
                        problem(ApiErrorCode.ACCESS_DENIED, COMPANY_PATH).match(result);
                    }
                });
        mvc.perform(patch(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer)
                        .contentType(MediaType.APPLICATION_JSON)
                        .content("{\"name\":\"Empresa HTTP actualizada\"}"))
                .andExpect(status().is(expectedStatus))
                .andExpect(result -> {
                    if (role == UserRole.USER) {
                        problem(ApiErrorCode.ACCESS_DENIED, COMPANY_PATH).match(result);
                    }
                });
        assertThat(context.getBeansOfType(UserDetailsService.class)).isEmpty();
    }

    /** El mismo JWT pierde acceso al confirmarse el cambio de ADMIN a USER en PostgreSQL. */
    @Test
    void reflectsCommittedRoleChangeWithSameJwt() throws Exception {
        User user = persistUser(UserRole.ADMIN);
        String bearer = "Bearer " + token(user.getId());
        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().isOk());

        new TransactionTemplate(transactionManager).executeWithoutResult(status ->
                entityManager.find(User.class, user.getId()).changeRole(UserRole.USER));

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(problem(ApiErrorCode.ACCESS_DENIED, COMPANY_PATH));
    }

    /** Un borrado confirmado invalida la identidad de un JWT todavía vigente. */
    @Test
    void rejectsCommittedUserDeletionWithSameJwt() throws Exception {
        User user = persistUser(UserRole.USER);
        String bearer = "Bearer " + token(user.getId());
        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().isForbidden());

        new TransactionTemplate(transactionManager).executeWithoutResult(status ->
                entityManager.remove(entityManager.find(User.class, user.getId())));

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().isUnauthorized())
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH));
    }

    /** Devuelve la compañía real y no acepta otra enviada en el JWT. */
    @Test
    void exposesOnlyCurrentUsersCompany() throws Exception {
        User userA = persistUser(UserRole.ADMIN);
        User userB = persistUser(UserRole.ADMIN);
        String bearerA = "Bearer " + token(userA.getId(),
                Map.of("companyId", userB.getCompany().getId(), "role", "USER"));

        var result = mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearerA))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.id").value(userA.getCompany().getId()))
                .andExpect(jsonPath("$.name").value(userA.getCompany().getName()))
                .andExpect(jsonPath("$.userId").doesNotExist())
                .andExpect(jsonPath("$.role").doesNotExist())
                .andExpect(jsonPath("$.passwordHash").doesNotExist())
                .andReturn();
        assertThat(result.getRequest().getSession(false)).isNull();

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, "Bearer " + token(userB.getId())))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.id").value(userB.getCompany().getId()));
        mvc.perform(get(COMPANY_PATH)).andExpect(status().isUnauthorized());
    }

    /** Simula el procesamiento interno de errores y conserva el estado original. */
    @ParameterizedTest
    @ValueSource(ints = {400, 401, 403, 404, 405, 406, 415, 500, 503})
    void preservesStatusDuringInternalErrorDispatch(int statusCode) throws Exception {
        mvc.perform(get("/error")
                        .accept(MediaType.APPLICATION_JSON)
                        .requestAttr(RequestDispatcher.ERROR_STATUS_CODE, statusCode)
                        .requestAttr(RequestDispatcher.ERROR_REQUEST_URI, COMPANY_PATH + "?token=private-test-value")
                        .requestAttr(RequestDispatcher.ERROR_EXCEPTION,
                                statusCode == 405
                                        ? new HttpRequestMethodNotSupportedException("POST", java.util.Set.of("GET", "PATCH"))
                                        : new IllegalStateException("Private SQL and token details"))
                        .with(request -> {
                            request.setDispatcherType(DispatcherType.ERROR);
                            return request;
                        }))
                .andExpect(problem(ApiErrorCode.fromStatus(statusCode), COMPANY_PATH));
    }

    /** Una petición normal a /error sigue sometida a la denegación de rutas no previstas. */
    @Test
    void rejectsDirectRequestsToErrorRoute() throws Exception {
        mvc.perform(get("/error")).andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, "/error"));

        User admin = persistUser(UserRole.ADMIN);
        mvc.perform(get("/error").header(HttpHeaders.AUTHORIZATION, "Bearer " + token(admin.getId())))
                .andExpect(problem(ApiErrorCode.ACCESS_DENIED, "/error"));
    }

    /** Crea y confirma un usuario con un ADMIN adicional para las pruebas de cambios. */
    private User persistUser(UserRole role) {
        return Objects.requireNonNull(new TransactionTemplate(transactionManager).execute(status -> {
            Company company = new Company("Empresa HTTP " + UUID.randomUUID());
            entityManager.persist(company);
            companyIds.add(company.getId());
            entityManager.persist(newUser(UserRole.ADMIN, company));
            User user = newUser(role, company);
            entityManager.persist(user);
            entityManager.flush();
            return user;
        }));
    }

    /** Evita colisiones de email entre ejecuciones y compañías. */
    private User newUser(UserRole role, Company company) {
        return new User("Ana", "García", UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, role, company);
    }

    /** Limpia los datos confirmados y el contexto local de autenticación. */
    @AfterEach
    void cleanUp() {
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
}
