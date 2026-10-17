package com.securebrowser.platform.company.dto;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import java.time.Instant;

import com.securebrowser.platform.company.Company;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.json.JsonTest;
import org.springframework.boot.test.json.JacksonTester;

/** Comprueba la conversión y representación JSON de la compañía. */
@JsonTest
class CompanyResponseJsonTest {

    // Herramienta de Spring para convertir el DTO a JSON.
    @Autowired
    private JacksonTester<CompanyResponse> json;

    /** Devuelve únicamente los campos públicos y las fechas en UTC. */
    @Test
    void serializesMappedCompanyWithOnlyApprovedFields() throws Exception {
        Company company = mock(Company.class);
        when(company.getId()).thenReturn(7L);
        when(company.getName()).thenReturn("Empresa A");
        when(company.getCreatedAt())
                .thenReturn(Instant.parse("2026-10-17T08:00:00Z"));
        when(company.getUpdatedAt())
                .thenReturn(Instant.parse("2026-10-17T09:00:00Z"));

        CompanyResponse response = CompanyResponse.from(company);

        assertThat(json.write(response)).isStrictlyEqualToJson("""
                {
                    "id": 7,
                    "name": "Empresa A",
                    "createdAt": "2026-10-17T08:00:00Z",
                    "updatedAt": "2026-10-17T09:00:00Z"
                }
                """);
    }
}