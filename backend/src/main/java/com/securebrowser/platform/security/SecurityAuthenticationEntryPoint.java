package com.securebrowser.platform.security;

import java.io.IOException;

import com.securebrowser.platform.exception.ApiErrorCode;
import com.securebrowser.platform.exception.ApiProblemDetailWriter;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.context.annotation.Import;
import org.springframework.http.HttpHeaders;
import org.springframework.security.core.AuthenticationException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.web.AuthenticationEntryPoint;
import org.springframework.stereotype.Component;

/** Responde a fallos de autenticación sin publicar tokens ni detalles internos. */
@Component
@Import(ApiProblemDetailWriter.class)
public final class SecurityAuthenticationEntryPoint implements AuthenticationEntryPoint {

    // Escribe el mismo formato público que utilizan los errores de MVC.
    private final ApiProblemDetailWriter problemWriter;

    /** Recibe el writer compartido mediante inyección por constructor. */
    public SecurityAuthenticationEntryPoint(ApiProblemDetailWriter problemWriter) {
        this.problemWriter = problemWriter;
    }

    /** Separa la indisponibilidad temporal de los rechazos Bearer. */
    @Override
    public void commence(HttpServletRequest request, HttpServletResponse response,
            AuthenticationException exception) throws IOException {
        if (exception instanceof OAuth2AuthenticationException oauth2Exception
                && AuthenticatedUserJwtConverter.TEMPORARILY_UNAVAILABLE.equals(
                        oauth2Exception.getError().getErrorCode())) {
            problemWriter.write(ApiErrorCode.SERVICE_UNAVAILABLE, request, response);
            return;
        }

        response.setHeader(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\"");
        problemWriter.write(ApiErrorCode.AUTHENTICATION_REQUIRED, request, response);
    }
}
