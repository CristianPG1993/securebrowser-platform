package com.securebrowser.platform.exception;

import static org.assertj.core.api.Assertions.assertThat;

import java.nio.charset.StandardCharsets;

import tools.jackson.databind.json.JsonMapper;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.json.JsonTest;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;

/** Comprueba el JSON escrito en la respuesta y la conservación de las cabeceras. */
@JsonTest
@Import(ApiProblemDetailWriter.class)
class ApiProblemDetailWriterTest {

    // Usa el writer y el mapper reales configurados por Spring Boot.
    @Autowired
    private ApiProblemDetailWriter writer;

    @Autowired
    private JsonMapper jsonMapper;

    /** Conserva el desafío Bearer y escribe un 401 sin copiar la query. */
    @Test
    void writesAuthenticationProblemAndPreservesChallenge() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        request.setQueryString("token=test-secret");
        MockHttpServletResponse response = new MockHttpServletResponse();
        response.setHeader(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\"");

        writer.write(ApiErrorCode.AUTHENTICATION_REQUIRED, request, response);

        assertThat(response.getStatus()).isEqualTo(401);
        assertThat(response.getContentType()).startsWith(MediaType.APPLICATION_PROBLEM_JSON_VALUE);
        assertThat(response.getCharacterEncoding()).isEqualTo(StandardCharsets.UTF_8.name());
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

    /** Escribe la indisponibilidad temporal sin añadir un desafío ni un plazo inventado. */
    @Test
    void writesServiceUnavailableProblemWithoutAuthenticationChallenge() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("PATCH", "/api/v1/companies/me");
        MockHttpServletResponse response = new MockHttpServletResponse();

        writer.write(ApiErrorCode.SERVICE_UNAVAILABLE, request, response);

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
}
