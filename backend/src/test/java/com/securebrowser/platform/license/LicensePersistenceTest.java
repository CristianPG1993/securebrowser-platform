package com.securebrowser.platform.license;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Optional;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/**
 * Comprueba la persistencia, auditoría y restricciones de License en PostgreSQL.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class LicensePersistenceTest {

    // Permite guardar y recuperar entidades.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite utilizar fechas conocidas en la prueba de auditoría.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Comprueba el identificador, la compañía, la capacidad y la auditoría. */
    @Test
    void persistAssignsIdAndAuditDates() {
        License license = persistLicense(10);
        Long id = license.getId();
        Long companyId = license.getCompany().getId();

        entityManager.clear();

        License stored = entityManager.find(License.class, id);

        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.getMaxInstallations()).isEqualTo(10);
        assertThat(stored.getCreatedAt()).isNotNull();
        assertThat(stored.getUpdatedAt()).isEqualTo(stored.getCreatedAt());
    }

    /** Conserva la creación y la compañía al cambiar capacidad y auditoría. */
    @Test
    void updatePreservesCreatedAtAndChangesUpdatedAt() {
        Instant creationTime = Instant.parse("2026-10-06T08:00:00Z");
        Instant modificationTime = creationTime.plusSeconds(60);

        try {
            auditingHandler.setDateTimeProvider(() -> Optional.of(creationTime));

            License license = persistLicense(10);
            Long id = license.getId();
            Long companyId = license.getCompany().getId();

            entityManager.clear();

            License stored = entityManager.find(License.class, id);

            assertThat(stored.getCreatedAt()).isEqualTo(creationTime);
            assertThat(stored.getUpdatedAt()).isEqualTo(creationTime);

            auditingHandler.setDateTimeProvider(() -> Optional.of(modificationTime));

            stored.changeMaxInstallations(20);
            entityManager.flush();
            entityManager.clear();

            License updated = entityManager.find(License.class, id);

            assertThat(updated.getMaxInstallations()).isEqualTo(20);
            assertThat(updated.getCompany().getId()).isEqualTo(companyId);
            assertThat(updated.getCreatedAt()).isEqualTo(creationTime);
            assertThat(updated.getUpdatedAt()).isEqualTo(modificationTime);
        } finally {
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** Comprueba el CHECK de capacidad sin pasar por la validación Java. */
    @ParameterizedTest
    @ValueSource(ints = {Integer.MIN_VALUE, -1, 0})
    void databaseRejectsInvalidCapacity(int capacity) {
        License license = persistLicense(10);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE licenses SET max_installations = :capacity WHERE id = :id")
                .setParameter("capacity", capacity)
                .setParameter("id", license.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_licenses_max_installations");
    }

    /** Comprueba que PostgreSQL exige una compañía. */
    @Test
    void databaseRejectsMissingCompany() {
        License license = persistLicense(10);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE licenses SET company_id = NULL WHERE id = :id")
                .setParameter("id", license.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("company_id");
    }

    /** Comprueba que la clave foránea rechaza una compañía inexistente. */
    @Test
    void databaseRejectsUnknownCompany() {
        License license = persistLicense(10);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE licenses SET company_id = :companyId WHERE id = :id")
                .setParameter("companyId", Long.MAX_VALUE)
                .setParameter("id", license.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_licenses_company");
    }

    /** Comprueba que una compañía con licencias no puede eliminarse. */
    @Test
    void databaseRejectsDeletingReferencedCompany() {
        License license = persistLicense(10);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "DELETE FROM companies WHERE id = :id")
                .setParameter("id", license.getCompany().getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_licenses_company");
    }

    /** Comprueba que eliminar una licencia conserva su compañía. */
    @Test
    void deletingLicenseDoesNotDeleteCompany() {
        License license = persistLicense(10);
        Long id = license.getId();
        Long companyId = license.getCompany().getId();

        entityManager.remove(license);
        entityManager.flush();
        entityManager.clear();

        assertThat(entityManager.find(License.class, id)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** Guarda una licencia con su compañía para la prueba. */
    private License persistLicense(int capacity) {
        Company company = new Company("Empresa de prueba de licencias");
        entityManager.persist(company);

        License license = new License(company, capacity);
        entityManager.persist(license);
        entityManager.flush();
        return license;
    }
}
