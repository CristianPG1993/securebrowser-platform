package com.securebrowser.platform.auth;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.HexFormat;

import org.springframework.stereotype.Component;

/** Calcula el hash persistido de los secretos de renovación, sin guardar el secreto original. */
@Component
public class RefreshTokenHasher {

    /** Aplica SHA-256 al texto exacto recibido y devuelve 64 caracteres hexadecimales. */
    public String hash(String rawToken) {
        if (rawToken == null || rawToken.isEmpty()) {
            throw new IllegalArgumentException("Refresh token is required");
        }

        try {
            // Cada llamada utiliza su propio digest, sin compartir estado entre peticiones.
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hashBytes = digest.digest(rawToken.getBytes(StandardCharsets.UTF_8));
            return HexFormat.of().formatHex(hashBytes);
        } catch (NoSuchAlgorithmException exception) {
            throw new IllegalStateException("SHA-256 is unavailable", exception);
        }
    }
}
