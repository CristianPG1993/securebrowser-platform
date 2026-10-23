package com.securebrowser.platform.auth.dto;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.security.oauth2.jwt.Jwt;

/** Comprueba los cuatro campos de la respuesta y la duración efectiva del token. */
class TokenResponseTest {

    /** La respuesta respeta tanto un JWT de 15 minutos como uno acortado a 3 minutos. */
    @ParameterizedTest
    @ValueSource(longs = { 900, 180 })
    void copiesTokensAndUsesEffectiveJwtLifetime(long lifetimeSeconds) {
        Instant issuedAt = Instant.parse("2026-10-22T08:00:00Z");
        // Objeto JWT de prueba: aquí comprobamos la conversión al DTO, no la firma.
        Jwt accessToken = Jwt.withTokenValue("access-token-test")
                .header("alg", "HS256")
                .subject("25")
                .issuedAt(issuedAt)
                .expiresAt(issuedAt.plusSeconds(lifetimeSeconds))
                .build();
        String refreshToken = "refresh-token-test";

        TokenResponse response = TokenResponse.from(accessToken, refreshToken);

        // Entrega el texto del access token recibido como argumento.
        assertThat(response.accessToken()).isEqualTo("access-token-test");
        // El tipo de autenticación siempre es Bearer, como establece el contrato.
        assertThat(response.tokenType()).isEqualTo("Bearer");
        // La duración coincide con exp menos iat: 900 o 180 segundos según el caso.
        assertThat(response.expiresIn()).isEqualTo(lifetimeSeconds);
        // Conserva el secreto de renovación recibido, sin generarlo ni modificarlo.
        assertThat(response.refreshToken()).isEqualTo(refreshToken);
    }
}
