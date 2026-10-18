package com.securebrowser.platform.security;

import java.util.Base64;

import javax.crypto.SecretKey;
import javax.crypto.spec.SecretKeySpec;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.bind.DefaultValue;

/** Conserva y valida la configuración externa utilizada para aceptar JWT. */
@ConfigurationProperties("sbp.security.jwt")
public final class JwtProperties {

    // Clave secreta del backend, preparada para verificar firmas HS256.
    private final SecretKey secretKey;

    // Identificador del emisor esperado en el claim iss del JWT.
    private final String issuer;

    // Identificador del destinatario esperado en el claim aud del JWT.
    private final String audience;

    // Duración máxima entre emisión (iat) y caducidad (exp), en segundos.
    private final long maxLifetimeSeconds;

    /**
     * Recibe la configuración y exige una clave y límites válidos.
     *
     * @param secretBase64 representación en Base64 de los bytes de la clave secreta
     * @param issuer identificador de quien emite los JWT aceptados
     * @param audience identificador de la API a la que se destinan los JWT
     * @param maxLifetimeSeconds duración máxima permitida; Spring aplica 900 por defecto
     */
    public JwtProperties(
            String secretBase64,
            String issuer,
            String audience,
            @DefaultValue("900") long maxLifetimeSeconds) {
        this.secretKey = decodeSecretKey(secretBase64);
        this.issuer = requireText(issuer, "JWT issuer is required");
        this.audience = requireText(audience, "JWT audience is required");

        if (maxLifetimeSeconds <= 0) {
            throw new IllegalArgumentException("JWT maximum lifetime must be positive");
        }
        this.maxLifetimeSeconds = maxLifetimeSeconds;
    }

    /** Decodifica la clave Base64 y exige al menos 32 bytes para HS256. */
    private static SecretKey decodeSecretKey(String secretBase64) {
        requireText(secretBase64, "JWT secret is required");

        // Bytes originales de la clave, recuperados desde su representación Base64.
        byte[] decoded;
        try {
            decoded = Base64.getDecoder().decode(secretBase64);
        } catch (IllegalArgumentException exception) {
            throw new IllegalArgumentException("JWT secret must be valid Base64");
        }

        if (decoded.length < 32) {
            throw new IllegalArgumentException("JWT secret must contain at least 32 bytes");
        }
        return new SecretKeySpec(decoded, "HmacSHA256");
    }

    /** Comprueba los valores obligatorios sin sustituirlos ni normalizarlos. */
    private static String requireText(String value, String message) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(message);
        }
        return value;
    }

    /** Proporciona la clave utilizada por el decodificador JWT. */
    public SecretKey getSecretKey() {
        return secretKey;
    }

    /** Devuelve el emisor que deben declarar los tokens aceptados. */
    public String getIssuer() {
        return issuer;
    }

    /** Devuelve la audiencia esperada para esta API. */
    public String getAudience() {
        return audience;
    }

    /** Devuelve la duración máxima permitida entre iat y exp, en segundos. */
    public long getMaxLifetimeSeconds() {
        return maxLifetimeSeconds;
    }
}
