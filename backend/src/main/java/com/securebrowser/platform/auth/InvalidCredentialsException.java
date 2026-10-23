package com.securebrowser.platform.auth;

import org.springframework.security.authentication.BadCredentialsException;

/** Representa el mismo fallo para un email desconocido y una contraseña incorrecta. */
public class InvalidCredentialsException extends BadCredentialsException {

    public InvalidCredentialsException() {
        super("Invalid credentials");
    }
}
