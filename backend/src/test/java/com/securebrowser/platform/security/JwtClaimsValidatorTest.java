package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.security.oauth2.core.OAuth2TokenValidatorResult;
import org.springframework.security.oauth2.jwt.Jwt;

/** Comprueba claims ya convertidos a tipos Java, utilizando un reloj fijo. */
class JwtClaimsValidatorTest {

    // Valores conocidos y aislados, utilizados exclusivamente en estas pruebas.
    private static final String TEST_SECRET =
            Base64.getEncoder().encodeToString(new byte[32]);
    private static final String ISSUER = "sbp-test";
    private static final String AUDIENCE = "sbp-api-test";
    private static final Instant NOW = Instant.parse("2026-10-18T08:00:00Z");

    private final JwtProperties properties =
            new JwtProperties(TEST_SECRET, ISSUER, AUDIENCE, 900);

    /** Acepta los seis claims obligatorios dentro del periodo de vigencia. */
    @Test
    void acceptsValidClaims() {
        assertThat(validatorAt(NOW).validate(jwt(validClaims())).hasErrors()).isFalse();
    }

    /** El identificador máximo de Long sigue siendo un subject válido. */
    @Test
    void acceptsMaximumLongSubject() {
        Map<String, Object> claims = validClaims();
        claims.put("sub", Long.toString(Long.MAX_VALUE));

        assertThat(validatorAt(NOW).validate(jwt(claims)).hasErrors()).isFalse();
    }

    /** Ninguno de los seis claims obligatorios puede omitirse. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "iss", "aud", "iat", "exp", "jti"})
    void rejectsMissingRequiredClaim(String claimName) {
        Map<String, Object> claims = validClaims();
        claims.remove(claimName);

        assertInvalid(validatorAt(NOW).validate(jwt(claims)));
    }

    /** Rechaza valores o tipos incorrectos sin revelar el contenido recibido. */
    @ParameterizedTest
    @MethodSource("invalidClaims")
    void rejectsInvalidClaimValueOrType(String claimName, Object value) {
        Map<String, Object> claims = validClaims();
        claims.put(claimName, value);

        assertInvalid(validatorAt(NOW).validate(jwt(claims)));
    }

    /** Acepta antes de exp y rechaza exactamente en exp y después. */
    @ParameterizedTest
    @ValueSource(longs = {-1L, 0L, 1L})
    void enforcesExpirationWithoutClockSkew(long offsetNanos) {
        Map<String, Object> claims = validClaims();
        Instant expiresAt = (Instant) claims.get("exp");
        JwtClaimsValidator validator = validatorAt(expiresAt.plusNanos(offsetNanos));

        assertThat(validator.validate(jwt(claims)).hasErrors())
                .isEqualTo(offsetNanos >= 0);
    }

    /** Rechaza un token cuya emisión se sitúa en el futuro. */
    @Test
    void rejectsFutureIssuedAt() {
        Map<String, Object> claims = validClaims();
        claims.put("iat", NOW.plusSeconds(1));

        assertInvalid(validatorAt(NOW).validate(jwt(claims)));
    }

    /** La caducidad debe ser posterior a la emisión. */
    @ParameterizedTest
    @ValueSource(longs = {-1L, 0L})
    void rejectsExpirationAtOrBeforeIssuedAt(long offsetSeconds) {
        Map<String, Object> claims = validClaims();
        Instant issuedAt = (Instant) claims.get("iat");
        claims.put("exp", issuedAt.plusSeconds(offsetSeconds));

        assertInvalid(validatorAt(NOW).validate(jwt(claims)));
    }

    /** Acepta el máximo de 900 segundos y rechaza un segundo adicional. */
    @ParameterizedTest
    @ValueSource(longs = {900L, 901L})
    void enforcesMaximumLifetime(long lifetimeSeconds) {
        Map<String, Object> claims = validClaims();
        Instant issuedAt = NOW.minusSeconds(1);
        claims.put("iat", issuedAt);
        claims.put("exp", issuedAt.plusSeconds(lifetimeSeconds));

        assertThat(validatorAt(NOW).validate(jwt(claims)).hasErrors())
                .isEqualTo(lifetimeSeconds > 900);
    }

    /** Si aparece nbf, se acepta desde ese instante y se rechaza antes. */
    @ParameterizedTest
    @ValueSource(longs = {-1L, 0L, 1L})
    void honorsOptionalNotBefore(long offsetSeconds) {
        Map<String, Object> claims = validClaims();
        claims.put("nbf", NOW.plusSeconds(offsetSeconds));

        assertThat(validatorAt(NOW).validate(jwt(claims)).hasErrors())
                .isEqualTo(offsetSeconds > 0);
    }

    /** Utiliza el límite configurado en lugar de fijar siempre 900 segundos. */
    @ParameterizedTest
    @ValueSource(longs = {60L, 61L})
    void honorsConfiguredMaximumLifetime(long lifetimeSeconds) {
        JwtProperties shorterLifetime =
                new JwtProperties(TEST_SECRET, ISSUER, AUDIENCE, 60);
        JwtClaimsValidator validator = new JwtClaimsValidator(
                shorterLifetime, Clock.fixed(NOW, ZoneOffset.UTC));

        Map<String, Object> claims = validClaims();
        Instant issuedAt = NOW.minusSeconds(1);
        claims.put("iat", issuedAt);
        claims.put("exp", issuedAt.plusSeconds(lifetimeSeconds));

        assertThat(validator.validate(jwt(claims)).hasErrors())
                .isEqualTo(lifetimeSeconds > 60);
    }

    /** Prepara un validador con el instante exacto que necesita cada prueba. */
    private JwtClaimsValidator validatorAt(Instant now) {
        return new JwtClaimsValidator(properties, Clock.fixed(now, ZoneOffset.UTC));
    }

    /** Prepara claims válidos que cada caso puede modificar independientemente. */
    private static Map<String, Object> validClaims() {
        Map<String, Object> claims = new LinkedHashMap<>();
        claims.put("sub", "25");
        claims.put("iss", ISSUER);
        claims.put("aud", List.of(AUDIENCE));
        claims.put("iat", NOW.minusSeconds(30));
        claims.put("exp", NOW.plusSeconds(30));
        claims.put("jti", "test-token-id");
        return claims;
    }

    /** Construye solo el objeto de claims; estas pruebas no verifican la firma. */
    private static Jwt jwt(Map<String, Object> claims) {
        return new Jwt("test-token", null, null, Map.of("alg", "HS256"), claims);
    }

    /** Comprueba el error genérico esperado para un token rechazado. */
    private static void assertInvalid(OAuth2TokenValidatorResult result) {
        assertThat(result.hasErrors()).isTrue();
        assertThat(result.getErrors()).singleElement().satisfies(error -> {
            assertThat(error.getErrorCode()).isEqualTo("invalid_token");
            assertThat(error.getDescription()).isEqualTo("Invalid bearer token");
        });
    }

    /** Valores incorrectos después de convertir los claims a sus tipos Java. */
    private static Stream<Arguments> invalidClaims() {
        return Stream.of(
                Arguments.of("sub", ""),
                Arguments.of("sub", " "),
                Arguments.of("sub", "0"),
                Arguments.of("sub", "-1"),
                Arguments.of("sub", "user-25"),
                Arguments.of("sub", "9223372036854775808"),
                Arguments.of("sub", 25L),
                Arguments.of("iss", "other-issuer"),
                Arguments.of("iss", 1L),
                Arguments.of("aud", AUDIENCE),
                Arguments.of("aud", List.of()),
                Arguments.of("aud", List.of("other-api")),
                Arguments.of("aud", List.of(AUDIENCE, 7)),
                Arguments.of("aud", List.of(AUDIENCE, " ")),
                Arguments.of("jti", ""),
                Arguments.of("jti", " "),
                Arguments.of("jti", 1L),
                Arguments.of("iat", "invalid-date"),
                Arguments.of("iat", NOW.getEpochSecond()),
                Arguments.of("exp", "invalid-date"),
                Arguments.of("exp", NOW.getEpochSecond()),
                Arguments.of("nbf", "invalid-date"),
                Arguments.of("nbf", NOW.getEpochSecond()));
    }
}
