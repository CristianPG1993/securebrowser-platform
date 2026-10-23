package com.securebrowser.platform.auth.dto;

import jakarta.validation.constraints.NotEmpty;

/** Identifica el grupo que se cerrará mediante el secreto original. */
public record LogoutRequest(@NotEmpty(message = "Refresh token must not be empty") String refreshToken) {
    @Override
    public String toString() {
        return "LogoutRequest[token=redacted]";
    }
}
