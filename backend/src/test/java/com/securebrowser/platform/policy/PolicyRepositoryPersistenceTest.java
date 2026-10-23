package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Optional;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba el ámbito de las consultas de Policy sobre PostgreSQL real y temporal. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
@Transactional
class PolicyRepositoryPersistenceTest {

    @Autowired
    private PolicyRepository policyRepository;

    @PersistenceContext
    private EntityManager entityManager;

    /** El id de otra compañía no permite recuperar su política. */
    @Test
    void findsPolicyOnlyWithinRequestedCompany() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        entityManager.persist(companyA);
        entityManager.persist(companyB);

        Policy policyA = new Policy("Política de prueba", companyA);
        Policy policyB = new Policy("Política de prueba", companyB);
        entityManager.persist(policyA);
        entityManager.persist(policyB);
        entityManager.flush();
        entityManager.clear();

        Optional<Policy> ownCompanyResult = policyRepository.findByIdAndCompany_Id(
                policyA.getId(), companyA.getId());

        // La política se encuentra al buscarla dentro de su propia compañía.
        assertThat(ownCompanyResult).isPresent();
        // El resultado corresponde exactamente a la política A, aunque B tenga el mismo nombre.
        assertThat(ownCompanyResult.orElseThrow().getId()).isEqualTo(policyA.getId());
        // La compañía del resultado es la compañía A.
        assertThat(ownCompanyResult.orElseThrow().getCompany().getId()).isEqualTo(companyA.getId());
        // Conocer el id de la política A no permite encontrarla desde la compañía B.
        assertThat(policyRepository.findByIdAndCompany_Id(policyA.getId(), companyB.getId())).isEmpty();
        // Un id inexistente también produce vacío, igual que uno ajeno a la compañía.
        assertThat(policyRepository.findByIdAndCompany_Id(Long.MAX_VALUE, companyA.getId())).isEmpty();
    }
}
