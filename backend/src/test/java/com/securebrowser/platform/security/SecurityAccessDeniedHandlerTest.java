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
import org.springframework.security.access.AccessDeniedException;

/** Comprueba el rechazo de permisos sin devolver causas internas ni datos de entrada. */
@JsonTest
@Import(SecurityAccessDeniedHandler.class)
class SecurityAccessDeniedHandlerTest {

    @Autowired
    private SecurityAccessDeniedHandler handler;

    @Autowired
    private JsonMapper mapper;

    @Test
    void writesGenericAccessDeniedProblem() throws Exception {
        MockHttpServletRequest request = new MockHttpServletRequest("PATCH", "/api/v1/companies/me");
        request.setQueryString("token=private-test-token");
        MockHttpServletResponse response = new MockHttpServletResponse();

        handler.handle(request, response, new AccessDeniedException("Private role and SQL detail"));

        assertThat(response.getStatus()).isEqualTo(403);
        assertThat(response.getContentType()).startsWith(MediaType.APPLICATION_PROBLEM_JSON_VALUE);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE)).isNull();
        assertThat(mapper.readTree(response.getContentAsByteArray())).isEqualTo(mapper.readTree("""
                {
                    "type": "about:blank",
                    "title": "Forbidden",
                    "status": 403,
                    "detail": "No tienes permiso para realizar esta operación.",
                    "instance": "/api/v1/companies/me",
                    "code": "ACCESS_DENIED"
                }
                """));
    }
}
