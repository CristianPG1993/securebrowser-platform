package com.securebrowser.platform.company;

import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRole;

import org.springframework.security.access.AccessDeniedException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Coordina las operaciones y reglas de negocio de Company. */
@Service
public class CompanyService {

    // Repositorio utilizado para acceder a las compañías.
    private final CompanyRepository companyRepository;

    /** Recibe el repositorio mediante inyección por constructor. */
    public CompanyService(CompanyRepository companyRepository) {
        this.companyRepository = companyRepository;
    }

    /**
     * Consulta la compañía del principal de confianza tras comprobar su rol ADMIN.
     */
    @Transactional(readOnly = true)
    public Company getOwnCompany(AuthenticatedUser authenticatedUser) {
        if (authenticatedUser == null) {
            throw new IllegalArgumentException("Authenticated user is required");
        }

        if (authenticatedUser.role() != UserRole.ADMIN) {
            throw new AccessDeniedException("Company administration requires ADMIN role");
        }

        return companyRepository.findById(authenticatedUser.companyId())
                .orElseThrow(CompanyNotFoundException::new);
    }

    /** Cambia el nombre de la compañía propia dentro de una transacción. */
    @Transactional
    public Company changeOwnCompanyName(AuthenticatedUser authenticatedUser, String name) {
        Company company = getOwnCompany(authenticatedUser);
        company.changeName(name);
        return companyRepository.save(company);
    }
}