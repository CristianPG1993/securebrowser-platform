package com.securebrowser.platform.exception;

import java.net.URI;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.DispatcherType;
import jakarta.servlet.RequestDispatcher;

import org.springframework.http.ProblemDetail;
import org.springframework.stereotype.Component;

/** Construye el cuerpo común que compartirán los errores de MVC y de seguridad. */
@Component
public final class ApiProblemDetailFactory {

    /** Crea un error público a partir del catálogo y de la ruta solicitada. */
    public ProblemDetail create(ApiErrorCode errorCode, HttpServletRequest request) {
        ProblemDetail problem = ProblemDetail.forStatusAndDetail(
                errorCode.getStatus(), errorCode.getDetail());

        problem.setType(URI.create("about:blank"));
        problem.setTitle(errorCode.getStatus().getReasonPhrase());
        problem.setInstance(instance(request));
        problem.setProperty("code", errorCode.name());

        return problem;
    }

    /** Conserva la ruta original en ERROR y excluye query, autoridad y URI inválidas. */
    private URI instance(HttpServletRequest request) {
        String path = request.getRequestURI();
        if (request.getDispatcherType() == DispatcherType.ERROR
                && request.getAttribute(RequestDispatcher.ERROR_REQUEST_URI) instanceof String originalPath) {
            path = originalPath;
        }
        int query = path.indexOf('?');
        if (query >= 0) {
            path = path.substring(0, query);
        }
        try {
            URI instance = URI.create(path);
            if (!instance.isAbsolute() && instance.getRawAuthority() == null
                    && instance.getRawPath() != null && instance.getRawPath().startsWith("/")) {
                return instance;
            }
        } catch (IllegalArgumentException ignored) {
            // Una ruta inválida no debe impedir la construcción del error público.
        }
        return URI.create("/");
    }
}
