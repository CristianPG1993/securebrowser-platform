package com.securebrowser.platform.auth.dto;

import java.time.Duration;

import org.springframework.security.oauth2.jwt.Jwt;

/** Representa los tokens entregados al cliente tras un login o una renovación correctos. */
public record TokenResponse(
        String accessToken,
        String tokenType,
        long expiresIn,
        String refreshToken) {

    /** Construye la respuesta usando la duración efectiva del JWT emitido por el backend. */
    public static TokenResponse from(Jwt accessToken, String refreshToken) {
        // Respeta también los JWT acortados por la caducidad del grupo de sesión.
        long expiresIn = Duration.between(accessToken.getIssuedAt(), accessToken.getExpiresAt()).getSeconds();

        return new TokenResponse(
                accessToken.getTokenValue(),
                "Bearer",
                expiresIn,
                refreshToken);
    }
}
