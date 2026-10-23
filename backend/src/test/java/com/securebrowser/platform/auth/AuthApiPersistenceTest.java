package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;
import static org.mockito.Mockito.doReturn;

import java.time.Clock;
import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Stream;

import com.securebrowser.platform.auth.dto.TokenResponse;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.company.CompanyRepository;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;
import tools.jackson.databind.json.JsonMapper;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.webmvc.test.autoconfigure.AutoConfigureMockMvc;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.context.bean.override.mockito.MockitoSpyBean;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.MvcResult;

/** Recorre filtros, validación, controllers, transacciones y PostgreSQL sin sustituir los servicios. */
@SpringBootTest(properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-auth-http-test",
        "sbp.security.jwt.audience=sbp-auth-http-api",
        "sbp.security.jwt.max-lifetime-seconds=900",
        "sbp.auth.refresh-token-lifetime-seconds=604800"
})
@AutoConfigureMockMvc
@Import({ PostgresRepositoryTestConfiguration.class, AuthServicePersistenceTest.FixedClockConfiguration.class })
class AuthApiPersistenceTest {
    private static final String BASE = "/api/v1/auth";
    private static final String PASSWORD = " Clave API demo 2026! ";
    private final List<Long> companyIds = new ArrayList<>();

    @Autowired private MockMvc mvc;
    @Autowired private JsonMapper mapper;
    @Autowired private CompanyRepository companies;
    @Autowired private UserRepository users;
    @Autowired private PasswordEncoder encoder;
    @Autowired private JdbcTemplate jdbc;
    @MockitoSpyBean(name = "authServiceTestClock") private Clock clock;

    @AfterEach
    void cleanup() {
        for (Long id : companyIds) {
            jdbc.update("delete from users where company_id = ?", id);
            jdbc.update("delete from companies where id = ?", id);
        }
    }

    @ParameterizedTest
    @EnumSource(UserRole.class)
    void loginProfileRefreshLogoutAndJwtExpiry(UserRole role) throws Exception {
        User user = createUser(role);
        TokenResponse login = login(user);
        MvcResult profile = mvc.perform(get("/api/v1/users/me")
                .header(HttpHeaders.AUTHORIZATION, "Bearer " + login.accessToken()))
                .andExpect(status().isOk()).andExpect(jsonPath("$.id").value(user.getId()))
                .andExpect(jsonPath("$.companyId").value(user.getCompany().getId()))
                .andExpect(jsonPath("$.role").value(role.name())).andReturn();
        Map<?, ?> fields = mapper.readValue(profile.getResponse().getContentAsString(), Map.class);
        // El perfil contiene exclusivamente los ocho campos del contrato, sin hashes ni tokens.
        assertThat(fields.keySet()).isEqualTo(Set.of(
                "id", "companyId", "name", "lastName", "email", "role", "createdAt", "updatedAt"));

        TokenResponse renewed = tokenResponse("/refresh", Map.of("refreshToken", login.refreshToken()));
        // El endpoint devuelve el secreto que sustituye al consumido.
        assertThat(renewed.refreshToken()).isNotEqualTo(login.refreshToken());
        mvc.perform(post(BASE + "/logout").contentType(MediaType.APPLICATION_JSON)
                .content(mapper.writeValueAsString(Map.of("refreshToken", renewed.refreshToken()))))
                .andExpect(status().isNoContent()).andExpect(content().string(""));
        mvc.perform(post(BASE + "/refresh").contentType(MediaType.APPLICATION_JSON)
                .content(mapper.writeValueAsString(Map.of("refreshToken", renewed.refreshToken()))))
                .andExpect(status().isUnauthorized()).andExpect(jsonPath("$.code").value("INVALID_REFRESH_TOKEN"));
        // Cerrar la renovación no introduce una lista de revocación inmediata de JWT.
        mvc.perform(get("/api/v1/users/me").header(HttpHeaders.AUTHORIZATION, "Bearer " + renewed.accessToken()))
                .andExpect(status().isOk());
        doReturn(Instant.parse("2026-10-23T08:15:00Z")).when(clock).instant();
        mvc.perform(get("/api/v1/users/me").header(HttpHeaders.AUTHORIZATION, "Bearer " + renewed.accessToken()))
                .andExpect(status().isUnauthorized()).andExpect(jsonPath("$.code").value("AUTHENTICATION_REQUIRED"));
    }

    @Test
    void invalidCredentialsHaveIdenticalGenericProblemResponses() throws Exception {
        User user = createUser(UserRole.USER);
        String wrong = credentialFailure(user.getEmail(), "Never show this password");
        String unknown = credentialFailure(UUID.randomUUID() + "@example.com", PASSWORD);
        // No se revela si el email existe ni se copia la contraseña a la respuesta.
        assertThat(wrong).isEqualTo(unknown).doesNotContain(user.getEmail(), "Never show this password", PASSWORD);
    }

    @Test
    void reuseReturns401AfterRevokingTheLatestRefresh() throws Exception {
        TokenResponse original = login(createUser(UserRole.USER));
        TokenResponse latest = tokenResponse("/refresh", Map.of("refreshToken", original.refreshToken()));
        for (String secret : List.of(original.refreshToken(), latest.refreshToken())) {
            mvc.perform(post(BASE + "/refresh").contentType(MediaType.APPLICATION_JSON)
                    .content(mapper.writeValueAsString(Map.of("refreshToken", secret))))
                    .andExpect(status().isUnauthorized()).andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON))
                    .andExpect(jsonPath("$.code").value("INVALID_REFRESH_TOKEN"))
                    .andExpect(header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\""));
        }
    }

    @ParameterizedTest
    @ValueSource(strings = { "/refresh", "/logout" })
    void unknownSecretReturnsGeneric401(String endpoint) throws Exception {
        mvc.perform(post(BASE + endpoint).contentType(MediaType.APPLICATION_JSON)
                .content("{\"refreshToken\":\"unknown-secret\"}"))
                .andExpect(status().isUnauthorized()).andExpect(jsonPath("$.code").value("INVALID_REFRESH_TOKEN"))
                .andExpect(header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\""));
    }

    @ParameterizedTest
    @MethodSource("invalidBodies")
    void invalidJsonAndValidationReturn400(String endpoint, String body) throws Exception {
        mvc.perform(post(BASE + endpoint).contentType(MediaType.APPLICATION_JSON).content(body))
                .andExpect(status().isBadRequest()).andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON))
                .andExpect(jsonPath("$.code").value("VALIDATION_ERROR"));
    }

    @Test
    void ownProfileUsesCurrentIdentityAndIgnoresAlternativeIds() throws Exception {
        User user = createUser(UserRole.USER);
        User other = createUser(UserRole.ADMIN);
        TokenResponse login = login(user);
        user.changeRole(UserRole.ADMIN);
        users.saveAndFlush(user);
        mvc.perform(get("/api/v1/users/me").param("userId", other.getId().toString())
                .param("companyId", other.getCompany().getId().toString())
                .header(HttpHeaders.AUTHORIZATION, "Bearer " + login.accessToken()))
                .andExpect(status().isOk()).andExpect(jsonPath("$.id").value(user.getId()))
                .andExpect(jsonPath("$.companyId").value(user.getCompany().getId()))
                .andExpect(jsonPath("$.role").value("ADMIN"));
        users.deleteById(user.getId());
        mvc.perform(get("/api/v1/users/me").header(HttpHeaders.AUTHORIZATION, "Bearer " + login.accessToken()))
                .andExpect(status().isUnauthorized()).andExpect(jsonPath("$.code").value("AUTHENTICATION_REQUIRED"));
    }

    @Test
    void profileRequiresBearerAndOtherUserCrudRemainsDenied() throws Exception {
        mvc.perform(get("/api/v1/users/me")).andExpect(status().isUnauthorized());
        mvc.perform(get("/api/v1/users/me").header(HttpHeaders.AUTHORIZATION, "Bearer invalid"))
                .andExpect(status().isUnauthorized());
        TokenResponse login = login(createUser(UserRole.ADMIN));
        mvc.perform(get("/api/v1/users").header(HttpHeaders.AUTHORIZATION, "Bearer " + login.accessToken()))
                .andExpect(status().isForbidden());
    }

    private String credentialFailure(String email, String password) throws Exception {
        return mvc.perform(post(BASE + "/login").contentType(MediaType.APPLICATION_JSON)
                .content(mapper.writeValueAsString(Map.of("email", email, "password", password))))
                .andExpect(status().isUnauthorized()).andExpect(jsonPath("$.code").value("INVALID_CREDENTIALS"))
                .andExpect(header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\""))
                .andReturn().getResponse().getContentAsString();
    }

    private TokenResponse login(User user) throws Exception {
        return tokenResponse("/login", Map.of("email", user.getEmail(), "password", PASSWORD));
    }

    private TokenResponse tokenResponse(String endpoint, Object body) throws Exception {
        String json = mvc.perform(post(BASE + endpoint).contentType(MediaType.APPLICATION_JSON)
                .content(mapper.writeValueAsString(body)))
                .andExpect(status().isOk()).andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(header().string(HttpHeaders.CACHE_CONTROL, "no-store"))
                .andExpect(header().string(HttpHeaders.PRAGMA, "no-cache"))
                .andExpect(jsonPath("$.tokenType").value("Bearer"))
                .andExpect(jsonPath("$.expiresIn").isNumber()).andReturn().getResponse().getContentAsString();
        // Las respuestas de tokens contienen únicamente los cuatro campos acordados.
        assertThat(mapper.readValue(json, Map.class).keySet()).isEqualTo(Set.of(
                "accessToken", "tokenType", "expiresIn", "refreshToken"));
        return mapper.readValue(json, TokenResponse.class);
    }

    private User createUser(UserRole role) {
        Company company = companies.saveAndFlush(new Company("Empresa de API auth"));
        companyIds.add(company.getId());
        return users.saveAndFlush(new User("Ana", "García", UUID.randomUUID() + "@example.com",
                encoder.encode(PASSWORD), role, company));
    }

    static Stream<Arguments> invalidBodies() {
        Stream<Arguments> secrets = Stream.of("/refresh", "/logout").flatMap(endpoint -> Stream.of(
                "", "null", "{}", "{\"refreshToken\":null}", "{\"refreshToken\":\"\"}",
                "{\"refreshToken\":12}", "{\"refreshToken\":\"dummy\",\"companyId\":1}", "{} {}")
                .map(body -> Arguments.of(endpoint, body)));
        Stream<Arguments> logins = Stream.of("", "null", "{}", "{\"email\":\"invalid\",\"password\":\"dummy\"}",
                "{\"email\":\"ana@example.com\",\"password\":true}",
                "{\"email\":\"ana@example.com\",\"password\":\"" + "a".repeat(73) + "\"}",
                "{\"email\":\"ana@example.com\",\"password\":\"dummy\",\"role\":\"ADMIN\"}")
                .map(body -> Arguments.of("/login", body));
        return Stream.concat(secrets, logins);
    }
}
