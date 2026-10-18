package com.securebrowser.platform.security;

import java.time.Clock;
import java.time.Duration;
import java.time.Instant;
import java.util.Collection;
import java.util.Map;
import java.util.Objects;

import org.springframework.security.oauth2.core.OAuth2Error;
import org.springframework.security.oauth2.core.OAuth2TokenValidator;
import org.springframework.security.oauth2.core.OAuth2TokenValidatorResult;
import org.springframework.security.oauth2.jwt.Jwt;

/** Comprueba los claims y la vigencia de un JWT cuya firma verifica el decoder. */
public final class JwtClaimsValidator implements OAuth2TokenValidator<Jwt> {

    // Error genérico que evita revelar los valores del token rechazado.
    private static final OAuth2Error INVALID_TOKEN =
            new OAuth2Error("invalid_token", "Invalid bearer token", null);

    // Emisor, audiencia y duración máxima configurados para esta API.
    private final JwtProperties properties;

    // Reloj del backend, sustituible por uno fijo en las pruebas.
    private final Clock clock;

    /** Recibe la configuración validada y el reloj utilizado para la caducidad. */
    public JwtClaimsValidator(JwtProperties properties, Clock clock) {
        this.properties = Objects.requireNonNull(properties, "JWT properties are required");
        this.clock = Objects.requireNonNull(clock, "Clock is required");
    }

    /** Exige los seis claims obligatorios y rechaza el token exactamente en exp. */
    @Override
    public OAuth2TokenValidatorResult validate(Jwt jwt) {
        Objects.requireNonNull(jwt, "JWT is required");
        Map<String, Object> claims = jwt.getClaims();

        if (!hasValidSubject(claims.get("sub"))
                || !properties.getIssuer().equals(claims.get("iss"))
                || !hasExpectedAudience(claims.get("aud"))
                || !(claims.get("jti") instanceof String tokenId)
                || tokenId.isBlank()) {
            return OAuth2TokenValidatorResult.failure(INVALID_TOKEN);
        }

        if (!(claims.get("iat") instanceof Instant issuedAt)
                || !(claims.get("exp") instanceof Instant expiresAt)) {
            return OAuth2TokenValidatorResult.failure(INVALID_TOKEN);
        }

        Instant now = clock.instant();
        if (issuedAt.isAfter(now)
                || !expiresAt.isAfter(issuedAt)
                || !now.isBefore(expiresAt)
                || Duration.between(issuedAt, expiresAt)
                        .compareTo(Duration.ofSeconds(properties.getMaxLifetimeSeconds())) > 0) {
            return OAuth2TokenValidatorResult.failure(INVALID_TOKEN);
        }

        // nbf es opcional; si aparece, el token no puede usarse antes de ese instante.
        if (claims.containsKey("nbf")
                && (!(claims.get("nbf") instanceof Instant notBefore)
                        || now.isBefore(notBefore))) {
            return OAuth2TokenValidatorResult.failure(INVALID_TOKEN);
        }

        return OAuth2TokenValidatorResult.success();
    }

    /** El subject debe representar el identificador positivo de un usuario. */
    private static boolean hasValidSubject(Object claim) {
        if (!(claim instanceof String subject)) {
            return false;
        }
        try {
            return Long.parseLong(subject) > 0;
        } catch (NumberFormatException exception) {
            return false;
        }
    }

    /** Exige una colección de audiencias textuales que incluya nuestra API. */
    private boolean hasExpectedAudience(Object claim) {
        return claim instanceof Collection<?> audiences
                && audiences.stream().allMatch(
                        value -> value instanceof String text && !text.isBlank())
                && audiences.contains(properties.getAudience());
    }
}
