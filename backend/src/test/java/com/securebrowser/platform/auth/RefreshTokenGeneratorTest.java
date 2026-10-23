package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Base64;

import org.junit.jupiter.api.Test;

/** Comprueba el formato y el tamaño de los secretos generados, sin base de datos. */
class RefreshTokenGeneratorTest {

    /** Cada llamada genera un secreto nuevo de 32 bytes representado en Base64 URL-safe. */
    @Test
    void generatesDifferentUrlSafeSecretsWith32Bytes() {
        RefreshTokenGenerator generator = new RefreshTokenGenerator();

        String firstToken = generator.generate();
        String secondToken = generator.generate();

        // El texto tiene 43 caracteres del alfabeto URL-safe y no contiene relleno con '='.
        assertThat(firstToken).matches("[A-Za-z0-9_-]{43}");
        // Al decodificar el texto recuperamos los 32 bytes originales del secreto.
        assertThat(Base64.getUrlDecoder().decode(firstToken)).hasSize(32);
        // Dos llamadas al mismo generador producen secretos distintos.
        assertThat(secondToken).isNotEqualTo(firstToken);
    }
}
