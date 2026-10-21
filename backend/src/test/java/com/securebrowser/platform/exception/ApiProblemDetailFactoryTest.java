package com.securebrowser.platform.exception;

import static org.assertj.core.api.Assertions.assertThat;

import java.net.URI;
import java.util.Map;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import jakarta.servlet.DispatcherType;
import jakarta.servlet.RequestDispatcher;
import org.springframework.http.ProblemDetail;
import org.springframework.mock.web.MockHttpServletRequest;

/** Comprueba el cuerpo común de errores y la exclusión de parámetros de consulta. */
class ApiProblemDetailFactoryTest {

    // La fábrica no necesita arrancar Spring para construir un ProblemDetail.
    private final ApiProblemDetailFactory factory = new ApiProblemDetailFactory();

    /** Un recurso no encontrado contiene los seis campos acordados por el contrato. */
    @Test
    void createsResourceNotFoundProblem() {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");

        ProblemDetail problem = factory.create(ApiErrorCode.RESOURCE_NOT_FOUND, request);

        assertThat(problem.getType()).isEqualTo(URI.create("about:blank"));
        assertThat(problem.getTitle()).isEqualTo("Not Found");
        assertThat(problem.getStatus()).isEqualTo(404);
        assertThat(problem.getDetail()).isEqualTo("No se ha encontrado el recurso solicitado.");
        assertThat(problem.getInstance()).isEqualTo(URI.create("/api/v1/companies/me"));
        assertThat(problem.getProperties())
                .containsExactlyEntriesOf(Map.of("code", "RESOURCE_NOT_FOUND"));
    }

    /** Los valores enviados en la query no se copian a la ruta ni a la explicación. */
    @Test
    void excludesQueryParametersFromProblem() {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        request.setQueryString("token=test-secret&password=test-password");

        ProblemDetail problem = factory.create(ApiErrorCode.AUTHENTICATION_REQUIRED, request);

        assertThat(problem.getInstance()).isEqualTo(URI.create("/api/v1/companies/me"));
        assertThat(problem.getStatus()).isEqualTo(401);
        assertThat(problem.getDetail()).isEqualTo("Se requiere una autenticación válida.");
        assertThat(problem.getProperties())
                .containsExactlyEntriesOf(Map.of("code", "AUTHENTICATION_REQUIRED"));
    }

    @Test
    void usesOriginalPathDuringErrorDispatchWithoutQuery() {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/error");
        request.setDispatcherType(DispatcherType.ERROR);
        request.setAttribute(RequestDispatcher.ERROR_REQUEST_URI, "/api/v1/companies/me?token=test-secret");

        ProblemDetail problem = factory.create(ApiErrorCode.INTERNAL_ERROR, request);

        assertThat(problem.getInstance()).isEqualTo(URI.create("/api/v1/companies/me"));
    }

    @Test
    void ignoresErrorPathAttributeOnNormalRequest() {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/api/v1/companies/me");
        request.setAttribute(RequestDispatcher.ERROR_REQUEST_URI, "/another-path");

        assertThat(factory.create(ApiErrorCode.RESOURCE_NOT_FOUND, request).getInstance())
                .isEqualTo(URI.create("/api/v1/companies/me"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"https://example.com/private", "//example.com/private", "/invalid path"})
    void fallsBackSafelyForInvalidErrorPath(String path) {
        MockHttpServletRequest request = new MockHttpServletRequest("GET", "/error");
        request.setDispatcherType(DispatcherType.ERROR);
        request.setAttribute(RequestDispatcher.ERROR_REQUEST_URI, path);

        assertThat(factory.create(ApiErrorCode.INTERNAL_ERROR, request).getInstance())
                .isEqualTo(URI.create("/"));
    }
}
