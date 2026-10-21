package com.securebrowser.platform.security;

import java.io.IOException;

import com.securebrowser.platform.exception.ApiErrorCode;
import com.securebrowser.platform.exception.ApiProblemDetailWriter;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.context.annotation.Import;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.security.web.access.AccessDeniedHandler;
import org.springframework.stereotype.Component;

/** Responde con el error común cuando una identidad autenticada carece de permiso. */
@Component
@Import(ApiProblemDetailWriter.class)
public final class SecurityAccessDeniedHandler implements AccessDeniedHandler {

    private final ApiProblemDetailWriter problemWriter;

    /** Recibe el writer compartido mediante inyección por constructor. */
    public SecurityAccessDeniedHandler(ApiProblemDetailWriter problemWriter) {
        this.problemWriter = problemWriter;
    }

    /** Devuelve un 403 genérico sin publicar el mensaje interno de la excepción. */
    @Override
    public void handle(HttpServletRequest request, HttpServletResponse response,
            AccessDeniedException exception) throws IOException {
        problemWriter.write(ApiErrorCode.ACCESS_DENIED, request, response);
    }
}
