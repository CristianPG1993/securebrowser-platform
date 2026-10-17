package com.securebrowser.platform.company.dto;

import java.time.Instant;

import com.securebrowser.platform.company.Company;

/** Representa los datos públicos de una compañía en la API. */
public record CompanyResponse(
        Long id,
        String name,
        Instant createdAt,
        Instant updatedAt) {

        /** Copia los datos públicos de la entidad al DTO de respuesta. */
        public static CompanyResponse from(Company company) {
            return new CompanyResponse(
                    company.getId(),
                    company.getName(),
                    company.getCreatedAt(),
                    company.getUpdatedAt());
}
}