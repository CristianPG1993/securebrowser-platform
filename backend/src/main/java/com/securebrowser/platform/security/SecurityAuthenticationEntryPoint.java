package com.securebrowser.platform.security;

import java.io.IOException;

import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import org.springframework.http.HttpHeaders;
import org.springframework.security.authentication.InsufficientAuthenticationException;
import org.springframework.security.core.AuthenticationException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.web.AuthenticationEntryPoint;
import org.springframework.stereotype.Component;

/** Responde a fallos de autenticación sin publicar tokens ni detalles internos. */
@Component
public final class SecurityAuthenticationEntryPoint implements AuthenticationEntryPoint {

    /** Separa la indisponibilidad temporal de los rechazos Bearer. */
    @Override
    public void commence(HttpServletRequest request, HttpServletResponse response,
            AuthenticationException exception) throws IOException {
        if (exception instanceof OAuth2AuthenticationException oauth2Exception
                && AuthenticatedUserJwtConverter.TEMPORARILY_UNAVAILABLE.equals(
                        oauth2Exception.getError().getErrorCode())) {
            response.setStatus(HttpServletResponse.SC_SERVICE_UNAVAILABLE);
            return;
        }

        String challenge = exception instanceof InsufficientAuthenticationException
                ? "Bearer" : "Bearer error=\"invalid_token\"";
        response.setHeader(HttpHeaders.WWW_AUTHENTICATE, challenge);
        response.setStatus(HttpServletResponse.SC_UNAUTHORIZED);
    }
}
