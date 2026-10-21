package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;

import tools.jackson.databind.json.JsonMapper;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.json.JsonTest;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.authentication.InsufficientAuthenticationException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.oauth2.core.OAuth2Error;

/** Comprueba estados y desafíos Bearer sin divulgar los detalles de las excepciones. */
@JsonTest
@Import(SecurityAuthenticationEntryPoint.class)
class SecurityAuthenticationEntryPointTest {

    // Spring construye el EntryPoint con el writer y la fábrica reales.
    @Autowired
    private SecurityAuthenticationEntryPoint entryPoint;

    @Autowired
    private JsonMapper jsonMapper;

    /** Solicita autenticación Bearer cuando no hay una identidad autenticada. */
    @Test
    void challengesMissingAuthentication() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(request, response,
                new InsufficientAuthenticationException("Internal detail"));

        assertAuthenticationProblem(response);
    }

    /** La respuesta no contiene el mensaje interno ni el token rechazado. */
    @Test
    void sanitizesInvalidCredentials() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        request.setQueryString("token=test-secret");
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(request, response,
                new BadCredentialsException("Secret token or database detail"));

        assertAuthenticationProblem(response);
    }

    /** Un fallo de disponibilidad devuelve 503 sin invitar a renovar credenciales. */
    @Test
    void distinguishesTemporaryUnavailability() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(request, response,
                new OAuth2AuthenticationException(new OAuth2Error("temporarily_unavailable"),
                        "Internal database detail"));

        assertThat(response.getStatus()).isEqualTo(503);
        assertThat(response.getContentType()).startsWith(MediaType.APPLICATION_PROBLEM_JSON_VALUE);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE)).isNull();
        assertThat(response.getHeader(HttpHeaders.RETRY_AFTER)).isNull();
        assertThat(jsonMapper.readTree(response.getContentAsByteArray()))
                .isEqualTo(jsonMapper.readTree("""
                        {
                            "type": "about:blank",
                            "title": "Service Unavailable",
                            "status": 503,
                            "detail": "El servicio no está disponible temporalmente.",
                            "instance": "/api/v1/companies/me",
                            "code": "SERVICE_UNAVAILABLE"
                        }
                        """));
    }

    /** La ausencia y el rechazo de credenciales producen el mismo error público. */
    private void assertAuthenticationProblem(MockHttpServletResponse response) throws Exception {
        assertThat(response.getStatus()).isEqualTo(401);
        assertThat(response.getContentType()).startsWith(MediaType.APPLICATION_PROBLEM_JSON_VALUE);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE)).isEqualTo("Bearer realm=\"sbp\"");
        assertThat(jsonMapper.readTree(response.getContentAsByteArray()))
                .isEqualTo(jsonMapper.readTree("""
                        {
                            "type": "about:blank",
                            "title": "Unauthorized",
                            "status": 401,
                            "detail": "Se requiere una autenticación válida.",
                            "instance": "/api/v1/companies/me",
                            "code": "AUTHENTICATION_REQUIRED"
                        }
                        """));
    }
}
