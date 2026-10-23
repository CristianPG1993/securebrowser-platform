package com.securebrowser.platform.auth;

import java.security.SecureRandom;
import java.util.Base64;

import org.springframework.stereotype.Component;

/** Genera los secretos de renovación que se entregarán al cliente. */
@Component
public class RefreshTokenGenerator {

    // Generador criptográfico, sin una semilla fija elegida por la aplicación.
    private final SecureRandom secureRandom = new SecureRandom();

    /** Genera 32 bytes aleatorios y los representa como texto Base64 URL-safe sin relleno. */
    public String generate() {
        byte[] bytes = new byte[32];
        secureRandom.nextBytes(bytes);
        return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);
    }
}
