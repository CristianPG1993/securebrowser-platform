package com.securebrowser.platform.security;

import static com.securebrowser.platform.security.SecurityHttpTestSupport.COMPANY_PATH;
import static com.securebrowser.platform.security.SecurityHttpTestSupport.NOW;
import static com.securebrowser.platform.security.SecurityHttpTestSupport.token;
import static com.securebrowser.platform.exception.ApiProblemAssertions.problem;
import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.request;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.util.Map;

import com.securebrowser.platform.user.UserRole;
import com.securebrowser.platform.exception.ApiErrorCode;

import jakarta.servlet.Filter;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.autoconfigure.ImportAutoConfiguration;
import org.springframework.boot.jackson.autoconfigure.JacksonAutoConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Import;
import org.springframework.core.env.MapPropertySource;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpMethod;
import org.springframework.mock.web.MockServletContext;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.core.context.SecurityContextHolder;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.web.context.support.AnnotationConfigWebApplicationContext;
import org.springframework.web.servlet.config.annotation.EnableWebMvc;

/** Prueba firmas y filtros reales; solo la resolución de PostgreSQL está simulada. */
class SecurityHttpTest {

    private AnnotationConfigWebApplicationContext context;
    private AuthenticatedUserResolver resolver;
    private MockMvc mvc;

    /** Prepara un contexto web aislado y registra el filtro real de Spring Security. */
    @BeforeEach
    void setUp() {
        context = new AnnotationConfigWebApplicationContext();
        context.setServletContext(new MockServletContext());
        context.getEnvironment().getPropertySources().addFirst(
                new MapPropertySource("jwt-test", SecurityHttpTestSupport.properties()));
        context.register(WebTestConfiguration.class);
        context.refresh();
        resolver = context.getBean(AuthenticatedUserResolver.class);
        when(resolver.resolve(25L)).thenReturn(new AuthenticatedUser(25L, 7L, UserRole.ADMIN));
        mvc = MockMvcBuilders.webAppContextSetup(context)
                .addFilters(context.getBean("springSecurityFilterChain", Filter.class)).build();
    }

    /** Una petición sin Bearer recibe un desafío 401 y no consulta al usuario. */
    @Test
    void rejectsMissingBearer() throws Exception {
        mvc.perform(get(COMPANY_PATH))
                .andExpect(status().isUnauthorized())
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH));
        verifyNoInteractions(resolver);
    }

    /** Ni Basic ni un token en la query sustituyen el encabezado Bearer. */
    @Test
    void rejectsAlternativeCredentials() throws Exception {
        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, "Basic dXNlcjpwYXNz"))
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH));
        mvc.perform(get(COMPANY_PATH).param("access_token", token(25L)))
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH));
        verifyNoInteractions(resolver);
    }

    /** Una firma incorrecta, un JSON inválido o un token caducado no acceden a PostgreSQL. */
    @ParameterizedTest
    @ValueSource(strings = {"malformed", "signature", "expired", "numeric-sub"})
    void rejectsInvalidJwtBeforeUserResolution(String scenario) throws Exception {
        String bearer = switch (scenario) {
            case "malformed" -> "not-a-jwt";
            case "signature" -> tamperSignature(token(25L));
            case "expired" -> token(25L, Map.of("exp", NOW.getEpochSecond()));
            case "numeric-sub" -> token(25L, Map.of("sub", 25L));
            default -> throw new IllegalArgumentException("Unknown test scenario");
        };

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, "Bearer " + bearer))
                .andExpect(status().isUnauthorized())
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH))
                .andReturn();
        verifyNoInteractions(resolver);
    }

    /** ADMIN accede a GET y PATCH; PATCH no exige un token CSRF en esta API Bearer. */
    @ParameterizedTest
    @ValueSource(strings = {"GET", "PATCH"})
    void acceptsAdminWithRealBearer(String method) throws Exception {
        var result = mvc.perform(request(HttpMethod.valueOf(method), COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token(25L)))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.userId").value(25))
                .andExpect(jsonPath("$.companyId").value(7))
                .andExpect(jsonPath("$.role").value("ADMIN"))
                .andExpect(jsonPath("$.passwordHash").doesNotExist())
                .andReturn();
        assertThat(result.getRequest().getSession(false)).isNull();
        assertThat(SecurityContextHolder.getContext().getAuthentication()).isNull();
        verify(resolver).resolve(25L);
    }

    /** USER no puede administrar Company aunque el JWT declare permisos ADMIN. */
    @ParameterizedTest
    @ValueSource(strings = {"GET", "PATCH"})
    void rejectsUserRoleAndIgnoresJwtPermissions(String method) throws Exception {
        when(resolver.resolve(25L)).thenReturn(new AuthenticatedUser(25L, 7L, UserRole.USER));

        mvc.perform(request(HttpMethod.valueOf(method), COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer "
                                + token(25L, Map.of("role", "ADMIN", "companyId", 999L))))
                .andExpect(problem(ApiErrorCode.ACCESS_DENIED, COMPANY_PATH));
    }

    /** Un usuario inexistente produce 401 aunque el JWT siga correctamente firmado. */
    @Test
    void rejectsDeletedUser() throws Exception {
        when(resolver.resolve(25L)).thenThrow(new BadCredentialsException("Internal user detail"));

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, "Bearer " + token(25L)))
                .andExpect(status().isUnauthorized())
                .andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, COMPANY_PATH))
                .andReturn();
    }

    /** El error de persistencia simulado recorre el filtro real y conserva el estado 503. */
    @Test
    void returns503WhenDatabaseIsUnavailable() throws Exception {
        when(resolver.resolve(25L)).thenThrow(new DataAccessResourceFailureException("Private SQL detail"));

        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, "Bearer " + token(25L)))
                .andExpect(status().isServiceUnavailable())
                .andExpect(problem(ApiErrorCode.SERVICE_UNAVAILABLE, COMPANY_PATH))
                .andReturn();
    }

    /** Toda ruta no acordada permanece cerrada, incluido el descubrimiento OAuth. */
    @ParameterizedTest
    @ValueSource(strings = {"/", "/login", "/api/v1/auth/login", "/api/v1/users",
            "/api/v1/companies/7", "/.well-known/oauth-protected-resource"})
    void deniesUnspecifiedRoutes(String path) throws Exception {
        mvc.perform(get(path)).andExpect(problem(ApiErrorCode.AUTHENTICATION_REQUIRED, path));
        mvc.perform(get(path).header(HttpHeaders.AUTHORIZATION, "Bearer " + token(25L)))
                .andExpect(problem(ApiErrorCode.ACCESS_DENIED, path));
    }

    /** Otros métodos HTTP no adquieren permiso por coincidir con la ruta de Company. */
    @ParameterizedTest
    @ValueSource(strings = {"POST", "PUT", "DELETE", "HEAD", "OPTIONS"})
    void deniesUnspecifiedMethods(String method) throws Exception {
        mvc.perform(request(HttpMethod.valueOf(method), COMPANY_PATH)
                        .header(HttpHeaders.AUTHORIZATION, "Bearer " + token(25L)))
                .andExpect(status().isForbidden());
    }

    /** Cada petición resuelve de nuevo el usuario y no reutiliza una sesión anterior. */
    @Test
    void refreshesPermissionsAndDoesNotKeepHttpSession() throws Exception {
        String bearer = "Bearer " + token(25L);
        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().isOk());
        when(resolver.resolve(25L)).thenReturn(new AuthenticatedUser(25L, 7L, UserRole.USER));
        mvc.perform(get(COMPANY_PATH).header(HttpHeaders.AUTHORIZATION, bearer))
                .andExpect(status().isForbidden());
        mvc.perform(get(COMPANY_PATH)).andExpect(status().isUnauthorized());
        verify(resolver, times(2)).resolve(25L);
    }

    /** La seguridad de métodos funciona también al llamar al bean sin pasar por HTTP. */
    @Test
    void appliesMethodSecurityToSpringManagedBean() {
        var probe = context.getBean(SecurityHttpTestSupport.MethodProtectedProbe.class);
        SecurityContextHolder.getContext().setAuthentication(new AuthenticatedUserAuthenticationToken(
                new AuthenticatedUser(25L, 7L, UserRole.USER)));
        assertThatThrownBy(probe::adminOperation).isInstanceOf(AccessDeniedException.class);

        SecurityContextHolder.getContext().setAuthentication(new AuthenticatedUserAuthenticationToken(
                new AuthenticatedUser(25L, 7L, UserRole.ADMIN)));
        probe.adminOperation();
    }

    /** Cambia un carácter significativo de la firma sin modificar sus claims. */
    private static String tamperSignature(String token) {
        int position = token.lastIndexOf('.') + 1;
        char replacement = token.charAt(position) == 'A' ? 'B' : 'A';
        return token.substring(0, position) + replacement + token.substring(position + 1);
    }

    /** Libera los beans y evita que una identidad de prueba escape a otra prueba. */
    @AfterEach
    void tearDown() {
        SecurityContextHolder.clearContext();
        if (context != null) {
            context.close();
        }
    }

    /** MVC, JWT y filtros reales, con un único sustituto para la consulta de identidad. */
    @TestConfiguration(proxyBeanMethods = false)
    @EnableWebMvc
    @ImportAutoConfiguration(JacksonAutoConfiguration.class)
    @Import({JwtConfiguration.class, SecurityConfiguration.class,
            AuthenticatedUserJwtConverter.class, SecurityAuthenticationEntryPoint.class,
            SecurityHttpTestSupport.ProbeConfiguration.class})
    static class WebTestConfiguration {

        /** Sustituye únicamente la resolución de identidad para aislar los filtros. */
        @Bean
        AuthenticatedUserResolver resolver() {
            return mock(AuthenticatedUserResolver.class);
        }
    }
}
