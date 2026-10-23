package com.securebrowser.platform.auth.validation;

import static org.assertj.core.api.Assertions.assertThat;

import jakarta.validation.Validation;
import jakarta.validation.Validator;
import jakarta.validation.ValidatorFactory;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

/** Comprueba la anotación con el validador real de Jakarta, sin arrancar Spring. */
class BcryptPasswordLengthTest {

    /** Una letra ASCII ocupa un byte, una á ocupa dos y este emoji ocupa cuatro en UTF-8. */
    @ParameterizedTest
    @CsvSource({
            "a, 72, 0",
            "a, 73, 1",
            "á, 36, 0",
            "á, 37, 1",
            "😀, 18, 0",
            "😀, 19, 1"
    })
    void validatesUtf8ByteLimit(String symbol, int repetitions, int expectedErrors) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            PasswordInput input = new PasswordInput(symbol.repeat(repetitions));

            var errors = validator.validate(input);

            // Acepta exactamente 72 bytes y devuelve un error cuando se supera el límite.
            assertThat(errors).hasSize(expectedErrors);
            errors.forEach(error -> {
                // El error corresponde al atributo password que lleva nuestra anotación.
                assertThat(error.getPropertyPath().toString()).isEqualTo("password");
                // La restricción proporciona el mensaje que podrá usar la respuesta de validación.
                assertThat(error.getMessage()).isEqualTo("Password must not exceed 72 UTF-8 bytes");
            });
        }
    }

    // Objeto mínimo de prueba: aplica la anotación al componente password del record.
    private record PasswordInput(@BcryptPasswordLength String password) {
    }
}
