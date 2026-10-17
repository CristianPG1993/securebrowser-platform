package com.securebrowser.platform.company.dto;

import jakarta.validation.constraints.NotBlank;

import org.hibernate.validator.constraints.CodePointLength;

/** Representa el nuevo nombre enviado para actualizar la compañía. */
public record CompanyUpdateRequest(
        @NotBlank(message = "Company name must not be blank") @CodePointLength(max = 150, message = "Company name must not exceed 150 characters") String name) {

    /** Elimina los espacios exteriores antes de validar el nombre. */
    public CompanyUpdateRequest {
        if (name != null) {
            name = name.strip();
        }
    }
}