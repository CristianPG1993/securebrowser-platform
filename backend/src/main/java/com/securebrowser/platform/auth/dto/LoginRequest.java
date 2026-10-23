package com.securebrowser.platform.auth.dto;

import java.util.Locale;

import com.securebrowser.platform.auth.validation.BcryptPasswordLength;

import jakarta.validation.constraints.Email;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotEmpty;
import jakarta.validation.constraints.Size;

/** Recibe las credenciales de login sin aplicar el mínimo de una contraseña nueva. */
public record LoginRequest(
        @NotBlank(message = "Email must not be blank")
        @Size(max = 254, message = "Email must not exceed 254 characters")
        // Conserva el formato de email admitido por la entidad User.
        @Email(message = "Email must be valid", regexp = "[a-z0-9!#$%&'*+/=?^_`{|}~-]+"
                + "(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*"
                + "@(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+"
                + "[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?")
        String email,

        @NotEmpty(message = "Password must not be empty")
        @BcryptPasswordLength
        String password) {

    /** Normaliza solo el email; espacios y mayúsculas de la contraseña se conservan. */
    public LoginRequest {
        if (email != null) {
            email = email.strip().toLowerCase(Locale.ROOT);
        }
    }

    @Override
    public String toString() {
        return "LoginRequest[credentials=redacted]";
    }
}
