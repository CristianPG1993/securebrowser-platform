package com.securebrowser.platform.company;

import com.securebrowser.platform.company.dto.CompanyResponse;
import com.securebrowser.platform.company.dto.CompanyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;

import jakarta.validation.Valid;

import org.springframework.http.MediaType;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/** Expone la consulta y modificación de la compañía del usuario autenticado. */
@RestController
@RequestMapping("/api/v1/companies/me")
public class CompanyController {

    // El servicio conserva los controles de rol, ámbito y reglas de negocio.
    private final CompanyService companyService;

    /** Recibe el servicio mediante inyección por constructor. */
    public CompanyController(CompanyService companyService) {
        this.companyService = companyService;
    }

    /** Devuelve únicamente el DTO público de la compañía del ADMIN. */
    @GetMapping(produces = MediaType.APPLICATION_JSON_VALUE)
    public CompanyResponse getOwnCompany(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser authenticatedUser) {
        return CompanyResponse.from(companyService.getOwnCompany(authenticatedUser));
    }

    /** Valida el nombre y construye la respuesta después de la transacción del servicio. */
    @PatchMapping(consumes = MediaType.APPLICATION_JSON_VALUE, produces = MediaType.APPLICATION_JSON_VALUE)
    public CompanyResponse updateOwnCompany(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser authenticatedUser,
            @Valid @RequestBody CompanyUpdateRequest request) {
        Company company = companyService.changeOwnCompanyName(authenticatedUser, request.name());
        return CompanyResponse.from(company);
    }
}
