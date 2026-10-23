package com.securebrowser.platform.auth.dto;

import static org.assertj.core.api.Assertions.assertThat;

import jakarta.validation.Validation;
import jakarta.validation.Validator;
import jakarta.validation.ValidatorFactory;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba las restricciones del login y que solo se normaliza el email. */
class LoginRequestTest {

    /** El login conserva la contraseña y no aplica el mínimo exigido a una contraseña nueva. */
    @Test
    void normalizesEmailAndPreservesPassword() {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            String password = " MiClave ";
            LoginRequest request = new LoginRequest("  ANA@EMPRESA.COM  ", password);

            // El email pierde los espacios exteriores y se convierte a minúsculas.
            assertThat(request.email()).isEqualTo("ana@empresa.com");
            // La contraseña conserva exactamente sus espacios y mayúsculas.
            assertThat(request.password()).isEqualTo(password);
            // Las credenciales son válidas para login, aunque la contraseña tenga menos de 12 caracteres.
            assertThat(validator.validate(request)).isEmpty();
        }
    }

    /** Una contraseña ausente o vacía incumple la obligatoriedad del login. */
    @ParameterizedTest
    @NullAndEmptySource
    void rejectsMissingOrEmptyPassword(String password) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            LoginRequest request = new LoginRequest("ana@empresa.com", password);

            var errors = validator.validate(request);

            // Tanto null como "" generan un único error asociado al campo password.
            assertThat(errors).extracting(error -> error.getPropertyPath().toString())
                    .containsExactly("password");
            // El error corresponde a la contraseña obligatoria, no al límite máximo de bytes.
            assertThat(errors).extracting(error -> error.getMessage())
                    .containsExactly("Password must not be empty");
        }
    }

    /** El login exige un email con contenido después de eliminar los espacios exteriores. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = { "   ", "\t\n", "\u2003" })
    void rejectsMissingOrBlankEmail(String email) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            LoginRequest request = new LoginRequest(email, "MiClave");

            var errors = validator.validate(request);

            // Todos los errores corresponden al email; la contraseña de esta prueba es válida.
            assertThat(errors).extracting(error -> error.getPropertyPath().toString())
                    .containsOnly("email");
            // Siempre se incumple NotBlank, aunque también pueda fallar el formato del email.
            assertThat(errors).extracting(error -> error.getMessage())
                    .contains("Email must not be blank");
        }
    }

    /** Rechaza emails con contenido que no cumplen el formato admitido por User. */
    @ParameterizedTest
    @ValueSource(strings = {
            "anaempresa.com",
            "ana@",
            "ana@@empresa.com",
            "ana @empresa.com",
            "ana@localhost"
    })
    void rejectsMalformedEmail(String email) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            LoginRequest request = new LoginRequest(email, "MiClave");

            var errors = validator.validate(request);

            // Solo falla el campo email; la contraseña cumple sus restricciones.
            assertThat(errors).extracting(error -> error.getPropertyPath().toString())
                    .containsExactly("email");
            // El mensaje identifica el fallo de formato definido en la anotación Email.
            assertThat(errors).extracting(error -> error.getMessage())
                    .containsExactly("Email must be valid");
        }
    }

    /** El máximo de 254 caracteres se comprueba después de normalizar el email. */
    @Test
    void validatesEmailLengthAfterNormalization() {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            // 64 caracteres locales, @ y un dominio de 189 caracteres: 254 en total.
            String maximumEmail = "a".repeat(64) + "@"
                    + "b".repeat(63) + "." + "c".repeat(63) + "." + "d".repeat(61);
            LoginRequest validRequest = new LoginRequest("  " + maximumEmail + "  ", "MiClave");

            // Los espacios exteriores se eliminan antes de comprobar el máximo.
            assertThat(validRequest.email()).isEqualTo(maximumEmail);
            // Un email de 254 caracteres con partes de tamaño válido se acepta.
            assertThat(validator.validate(validRequest)).isEmpty();

            LoginRequest oversizedRequest = new LoginRequest(maximumEmail + "d", "MiClave");
            var errors = validator.validate(oversizedRequest);

            // Añadir un carácter produce errores solo en el campo email.
            assertThat(errors).extracting(error -> error.getPropertyPath().toString())
                    .containsOnly("email");
            // Con 255 caracteres se incumple expresamente la restricción Size.
            assertThat(errors).extracting(error -> error.getMessage())
                    .contains("Email must not exceed 254 characters");
        }
    }

    /** El DTO aplica el límite de bytes a letras ASCII, tildes y emojis. */
    @ParameterizedTest
    @CsvSource({
            "a, 72, 0",
            "a, 73, 1",
            "á, 36, 0",
            "á, 37, 1",
            "😀, 18, 0",
            "😀, 19, 1"
    })
    void validatesPasswordByteLimit(String symbol, int repetitions, int expectedErrors) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();
            LoginRequest request = new LoginRequest("ana@empresa.com", symbol.repeat(repetitions));

            var errors = validator.validate(request);

            // Acepta contraseñas de 72 bytes y rechaza las que superan ese límite.
            assertThat(errors).hasSize(expectedErrors);
            errors.forEach(error -> {
                // La anotación del DTO asocia el error al campo password.
                assertThat(error.getPropertyPath().toString()).isEqualTo("password");
                // La causa del rechazo es el máximo de bytes, no la obligatoriedad.
                assertThat(error.getMessage()).isEqualTo("Password must not exceed 72 UTF-8 bytes");
            });
        }
    }
}
