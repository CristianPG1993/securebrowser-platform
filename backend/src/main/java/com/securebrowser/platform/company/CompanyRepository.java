package com.securebrowser.platform.company;

import org.springframework.data.jpa.repository.JpaRepository;

/** Proporciona acceso a la persistencia de Company mediante Spring Data JPA. */
public interface CompanyRepository extends JpaRepository<Company, Long> {
}