package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba el cálculo de hashes con valores SHA-256 conocidos y sin base de datos. */
class RefreshTokenHasherTest {

    /** Cada secreto produce su hash SHA-256 exacto, representado en hexadecimal. */
    @ParameterizedTest
    @CsvSource({
            "123, a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3",
            "456, b3a8e0e1f9ab1bfe3a36f231f676f78bb30a519d2b21e6c530c0eee8ebb4a5d0"
    })
    void producesExpectedSha256Hash(String rawToken, String expectedHash) {
        RefreshTokenHasher hasher = new RefreshTokenHasher();

        String actualHash = hasher.hash(rawToken);

        // El resultado debe coincidir con el SHA-256 conocido del texto recibido.
        assertThat(actualHash).isEqualTo(expectedHash);
    }

    /** Las diferencias de espacios y mayúsculas no se eliminan antes de calcular el hash. */
    @ParameterizedTest
    @ValueSource(strings = { " abc", "abc ", "abc\n", "ABC" })
    void doesNotNormalizeTokenBeforeHashing(String differentToken) {
        RefreshTokenHasher hasher = new RefreshTokenHasher();
        String originalHash = hasher.hash("abc");

        String differentHash = hasher.hash(differentToken);

        // Añadir espacios o cambiar las mayúsculas representa otro secreto y produce otro hash.
        assertThat(differentHash).isNotEqualTo(originalHash);
    }

    /** Sin secreto no se puede calcular un hash utilizable para renovar una sesión. */
    @ParameterizedTest
    @NullAndEmptySource
    void rejectsMissingOrEmptyToken(String rawToken) {
        RefreshTokenHasher hasher = new RefreshTokenHasher();

        // Rechaza null y "" mediante la excepción prevista, sin devolver un hash.
        assertThatThrownBy(() -> hasher.hash(rawToken))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("Refresh token is required");
    }

    /** Un secreto real generado puede localizarse después mediante el mismo hash. */
    @Test
    void producesStableHexHashForGeneratedToken() {
        RefreshTokenGenerator generator = new RefreshTokenGenerator();
        RefreshTokenHasher hasher = new RefreshTokenHasher();
        String rawToken = generator.generate();

        String storedHash = hasher.hash(rawToken);
        String lookupHash = hasher.hash(rawToken);

        // El valor que se persistirá tiene los 64 caracteres hexadecimales admitidos por RefreshToken.
        assertThat(storedHash).matches("[0-9a-f]{64}");
        // Volver a recibir el mismo secreto permite calcular el mismo hash para buscarlo.
        assertThat(lookupHash).isEqualTo(storedHash);
    }
}
