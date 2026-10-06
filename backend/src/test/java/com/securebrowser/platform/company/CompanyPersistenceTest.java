package com.securebrowser.platform.company;

import static org.assertj.core.api.Assertions.assertThat;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import java.time.Instant;
import java.util.Optional;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;

/**
 * Comprueba la persistencia y auditoría de Company en PostgreSQL.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class CompanyPersistenceTest {

    // Permite guardar y recuperar entidades mediante JPA.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite controlar el proveedor de fechas durante las pruebas.
    @Autowired
    private AuditingHandler auditingHandler;

    /**
     * Comprueba que el guardado genera el identificador
     * y asigna ambas fechas al mismo instante.
     */
    @Test
    void persistAssignsIdAndAuditDates() {
        Company company = new Company("  Empresa de prueba  ");

        entityManager.persist(company);
        entityManager.flush();

        Long id = company.getId();
        entityManager.clear();

        Company storedCompany = entityManager.find(Company.class, id);

        assertThat(storedCompany).isNotNull();
        assertThat(storedCompany.getId()).isPositive();
        assertThat(storedCompany.getName()).isEqualTo("Empresa de prueba");
        assertThat(storedCompany.getCreatedAt()).isNotNull();
        assertThat(storedCompany.getUpdatedAt())
                .isEqualTo(storedCompany.getCreatedAt());
    }

    /**
     * Comprueba que una modificación conserva la fecha de creación
     * y actualiza la fecha de última modificación.
     */
    @Test
    void updatePreservesCreatedAtAndChangesUpdatedAt() {
        Instant creationTime = Instant.parse("2026-10-06T08:00:00Z");
        Instant modificationTime = creationTime.plusSeconds(60);

        try {
            // Proporciona un instante conocido para la creación.
            auditingHandler.setDateTimeProvider(() -> Optional.of(creationTime));

            Company company = new Company("Empresa inicial");
            entityManager.persist(company);
            entityManager.flush();

            Long id = company.getId();
            entityManager.clear();

            Company storedCompany = entityManager.find(Company.class, id);

            assertThat(storedCompany.getCreatedAt()).isEqualTo(creationTime);
            assertThat(storedCompany.getUpdatedAt()).isEqualTo(creationTime);

            // Proporciona otro instante para la modificación.
            auditingHandler.setDateTimeProvider(() -> Optional.of(modificationTime));

            storedCompany.changeName("Empresa modificada");
            entityManager.flush();
            entityManager.clear();

            Company updatedCompany = entityManager.find(Company.class, id);

            assertThat(updatedCompany.getName()).isEqualTo("Empresa modificada");
            assertThat(updatedCompany.getCreatedAt()).isEqualTo(creationTime);
            assertThat(updatedCompany.getUpdatedAt()).isEqualTo(modificationTime);
        } finally {
            // Restaura el proveedor habitual incluso si la prueba falla.
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /**
     * Comprueba que dos compañías pueden compartir nombre
     * y reciben identificadores distintos.
     */
    @Test
    void persistAllowsCompaniesWithSameName() {
        Company firstCompany = new Company("Empresa repetida");
        Company secondCompany = new Company("Empresa repetida");

        entityManager.persist(firstCompany);
        entityManager.persist(secondCompany);
        entityManager.flush();

        assertThat(firstCompany.getId()).isPositive();
        assertThat(secondCompany.getId())
                .isPositive()
                .isNotEqualTo(firstCompany.getId());
    }
}