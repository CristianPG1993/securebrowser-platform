package com.securebrowser.platform.auth.validation;

import java.nio.charset.StandardCharsets;

import jakarta.validation.ConstraintValidator;
import jakarta.validation.ConstraintValidatorContext;

/** Comprueba la longitud en bytes; la obligatoriedad se valida por separado en el DTO. */
public class BcryptPasswordLengthValidator implements ConstraintValidator<BcryptPasswordLength, String> {

    /** Acepta como máximo 72 bytes UTF-8 y conserva espacios y mayúsculas de la contraseña. */
    @Override
    public boolean isValid(String value, ConstraintValidatorContext context) {
        return value == null || value.getBytes(StandardCharsets.UTF_8).length <= 72;
    }
}
