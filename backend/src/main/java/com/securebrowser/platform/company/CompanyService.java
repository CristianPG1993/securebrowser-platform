package com.securebrowser.platform.company;

import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

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
     * Consulta la compañía del usuario de confianza tras comprobar su permiso
     * ADMIN.
     */
    @Transactional(readOnly = true)
    public Company getOwnCompany(User authenticatedUser) {
        if (authenticatedUser == null) {
            throw new IllegalArgumentException("Authenticated user is required");
        }

        if (authenticatedUser.getRole() != UserRole.ADMIN) {
            throw new SecurityException("Company administration requires ADMIN role");
        }

        Company company = authenticatedUser.getCompany();
        if (company == null || company.getId() == null) {
            throw new IllegalArgumentException("A persisted company is required");
        }

        return companyRepository.findById(company.getId())
                .orElseThrow(CompanyNotFoundException::new);
    }

    /** Cambia el nombre de la compañía propia dentro de una transacción. */
    @Transactional
    public Company changeOwnCompanyName(User authenticatedUser, String name) {
        Company company = getOwnCompany(authenticatedUser);
        company.changeName(name);
        return companyRepository.save(company);
    }
}