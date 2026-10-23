package com.securebrowser.platform.auth.dto;

import jakarta.validation.constraints.NotEmpty;

/** Conserva exactamente el secreto recibido: no se recorta ni normaliza. */
public record RefreshRequest(@NotEmpty(message = "Refresh token must not be empty") String refreshToken) {
    @Override
    public String toString() {
        return "RefreshRequest[token=redacted]";
    }
}
