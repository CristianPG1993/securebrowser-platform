package com.securebrowser.platform.auth;

import java.time.Clock;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;
import java.util.UUID;

import com.securebrowser.platform.security.JwtProperties;

import org.springframework.security.oauth2.jose.jws.MacAlgorithm;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtClaimsSet;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.JwtEncoderParameters;
import org.springframework.security.oauth2.jwt.JwsHeader;
import org.springframework.stereotype.Service;

/** Emite los JWT de acceso respetando la configuración y el límite del grupo de sesión. */
@Service
public class AccessTokenService {

    // Firmador preparado por JwtConfiguration con la clave secreta del backend.
    private final JwtEncoder jwtEncoder;

    // Emisor, audiencia y duración máxima del JWT.
    private final JwtProperties properties;

    // Reloj del backend, sustituible por uno fijo en las pruebas.
    private final Clock clock;

    /** Recibe las dependencias mediante inyección por constructor. */
    public AccessTokenService(JwtEncoder jwtEncoder, JwtProperties properties, Clock clock) {
        this.jwtEncoder = jwtEncoder;
        this.properties = properties;
        this.clock = clock;
    }

    /** El login o refresh proporciona el usuario validado y la caducidad del grupo persistido. */
    public Jwt issue(Long userId, Instant sessionExpiresAt) {
        if (userId == null || userId <= 0) {
            throw new IllegalArgumentException("A positive user ID is required");
        }
        if (sessionExpiresAt == null) {
            throw new IllegalArgumentException("Session expiry is required");
        }

        // NumericDate se emite con precisión de segundos en los JWT del backend.
        Instant issuedAt = clock.instant().truncatedTo(ChronoUnit.SECONDS);
        Instant expiresAt = issuedAt.plusSeconds(properties.getMaxLifetimeSeconds());
        Instant sessionLimit = sessionExpiresAt.truncatedTo(ChronoUnit.SECONDS);
        if (sessionLimit.isBefore(expiresAt)) {
            expiresAt = sessionLimit;
        }
        if (!expiresAt.isAfter(issuedAt)) {
            throw new IllegalArgumentException("Session expiry must allow a positive JWT lifetime");
        }

        JwsHeader header = JwsHeader.with(MacAlgorithm.HS256).type("JWT").build();
        JwtClaimsSet claims = JwtClaimsSet.builder()
                .subject(userId.toString())
                .issuer(properties.getIssuer())
                .audience(List.of(properties.getAudience()))
                .issuedAt(issuedAt)
                .expiresAt(expiresAt)
                .id(UUID.randomUUID().toString())
                .build();

        return jwtEncoder.encode(JwtEncoderParameters.from(header, claims));
    }
}
