package com.securebrowser.platform.exception;

import java.util.List;

import jakarta.servlet.RequestDispatcher;
import jakarta.servlet.http.HttpServletRequest;

import org.springframework.boot.webmvc.error.ErrorController;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.ProblemDetail;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.HttpRequestMethodNotSupportedException;

/** Aplica el formato común a los errores que el contenedor procesa internamente. */
@RestController
public final class ApiErrorController implements ErrorController {

    private final ApiProblemDetailFactory problemFactory;

    public ApiErrorController(ApiProblemDetailFactory problemFactory) {
        this.problemFactory = problemFactory;
    }

    /** Sustituye la respuesta de error de Boot sin abrir /error a peticiones normales. */
    @RequestMapping("${server.error.path:/error}")
    public ResponseEntity<ProblemDetail> error(HttpServletRequest request) {
        Object errorStatus = request.getAttribute(RequestDispatcher.ERROR_STATUS_CODE);
        ApiErrorCode code = ApiErrorCode.fromStatus(errorStatus instanceof Integer status ? status : 500);
        ProblemDetail problem = problemFactory.create(code, request);
        if (code == ApiErrorCode.VALIDATION_ERROR) {
            problem.setProperty("errors", List.of(
                    new ApiFieldError("request", "La petición no es válida.")));
        }
        HttpHeaders headers = new HttpHeaders();
        if (code == ApiErrorCode.AUTHENTICATION_REQUIRED) {
            headers.set(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\"");
        }
        if (code == ApiErrorCode.METHOD_NOT_ALLOWED
                && request.getAttribute(RequestDispatcher.ERROR_EXCEPTION)
                        instanceof HttpRequestMethodNotSupportedException methodError) {
            var supportedMethods = methodError.getSupportedHttpMethods();
            if (supportedMethods != null) {
                headers.setAllow(supportedMethods);
            }
        }
        return ResponseEntity.status(code.getStatus()).headers(headers)
                .contentType(MediaType.APPLICATION_PROBLEM_JSON).body(problem);
    }
}
