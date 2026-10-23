package com.securebrowser.platform.auth;

import org.springframework.security.authentication.BadCredentialsException;

/** Rechazo genérico que no revela el estado ni la existencia del secreto presentado. */
public class InvalidRefreshTokenException extends BadCredentialsException {
    public InvalidRefreshTokenException() {
        super("Invalid refresh token");
    }
}
