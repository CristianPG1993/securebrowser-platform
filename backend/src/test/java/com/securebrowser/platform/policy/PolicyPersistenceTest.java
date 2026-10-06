package com.securebrowser.platform.policy;

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
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/**
 * Comprueba la configuración, auditoría y restricciones de Policy en PostgreSQL.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class PolicyPersistenceTest {

    // Permite guardar y recuperar entidades.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite utilizar fechas conocidas en la prueba de auditoría.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Comprueba el identificador, la configuración inicial y las fechas. */
    @Test
    void persistAssignsIdAndAuditDates() {
        Company company = persistCompany("Empresa A");
        Policy policy = persistPolicy("  Política A  ", company);
        Long id = policy.getId();
        Long companyId = company.getId();

        entityManager.clear();

        Policy stored = entityManager.find(Policy.class, id);

        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getName()).isEqualTo("Política A");
        assertThat(stored.getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.isUrlFilteringEnabled()).isTrue();
        assertThat(stored.getUrlFilteringMode()).isEqualTo(FilterMode.DENYLIST);
        assertThat(stored.isDownloadControlEnabled()).isTrue();
        assertThat(stored.getDownloadControlMode()).isEqualTo(FilterMode.DENYLIST);
        assertThat(stored.getCreatedAt()).isNotNull();
        assertThat(stored.getUpdatedAt()).isEqualTo(stored.getCreatedAt());
    }

    /** Conserva la creación y la compañía al modificar la configuración. */
    @Test
    void updatePreservesCreatedAtAndChangesUpdatedAt() {
        Instant creationTime = Instant.parse("2026-10-06T08:00:00Z");
        Instant modificationTime = creationTime.plusSeconds(60);

        try {
            auditingHandler.setDateTimeProvider(() -> Optional.of(creationTime));

            Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));
            Long id = policy.getId();
            Long companyId = policy.getCompany().getId();

            entityManager.clear();

            Policy stored = entityManager.find(Policy.class, id);

            assertThat(stored.getCreatedAt()).isEqualTo(creationTime);
            assertThat(stored.getUpdatedAt()).isEqualTo(creationTime);

            auditingHandler.setDateTimeProvider(() -> Optional.of(modificationTime));

            stored.changeName("Política B");
            stored.configureUrlFiltering(false, FilterMode.ALLOWLIST);
            stored.configureDownloadControl(false, FilterMode.ALLOWLIST);
            entityManager.flush();
            entityManager.clear();

            Policy updated = entityManager.find(Policy.class, id);

            assertThat(updated.getName()).isEqualTo("Política B");
            assertThat(updated.getCompany().getId()).isEqualTo(companyId);
            assertThat(updated.isUrlFilteringEnabled()).isFalse();
            assertThat(updated.getUrlFilteringMode()).isEqualTo(FilterMode.ALLOWLIST);
            assertThat(updated.isDownloadControlEnabled()).isFalse();
            assertThat(updated.getDownloadControlMode()).isEqualTo(FilterMode.ALLOWLIST);
            assertThat(updated.getCreatedAt()).isEqualTo(creationTime);
            assertThat(updated.getUpdatedAt()).isEqualTo(modificationTime);
        } finally {
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** Admite nombres repetidos dentro de una compañía y entre compañías. */
    @Test
    void persistAllowsPoliciesWithSameName() {
        Company company = persistCompany("Empresa A");
        Policy first = persistPolicy("Política repetida", company);
        Policy second = persistPolicy("Política repetida", company);
        Policy third = persistPolicy("Política repetida", persistCompany("Empresa B"));

        assertThat(first.getId()).isPositive();
        assertThat(second.getId()).isPositive().isNotEqualTo(first.getId());
        assertThat(third.getId()).isPositive()
                .isNotEqualTo(first.getId()).isNotEqualTo(second.getId());
    }

    /** Persiste ambos modos incluso cuando sus funcionalidades están desactivadas. */
    @ParameterizedTest
    @EnumSource(FilterMode.class)
    void persistsModesWithDisabledFeatures(FilterMode mode) {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));
        Long id = policy.getId();

        policy.configureUrlFiltering(false, mode);
        policy.configureDownloadControl(false, mode);
        entityManager.flush();
        entityManager.clear();

        Policy stored = entityManager.find(Policy.class, id);

        assertThat(stored.isUrlFilteringEnabled()).isFalse();
        assertThat(stored.getUrlFilteringMode()).isEqualTo(mode);
        assertThat(stored.isDownloadControlEnabled()).isFalse();
        assertThat(stored.getDownloadControlMode()).isEqualTo(mode);
    }

    /** Comprueba los CHECK de modos con las funcionalidades desactivadas. */
    @ParameterizedTest
    @CsvSource({
            "url_filtering_mode, ck_policies_url_filtering_mode",
            "download_control_mode, ck_policies_download_control_mode"
    })
    void databaseRejectsUnknownModes(String column, String constraint) {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));
        policy.configureUrlFiltering(false, FilterMode.DENYLIST);
        policy.configureDownloadControl(false, FilterMode.DENYLIST);
        entityManager.flush();

        // La columna procede únicamente de los casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE policies SET " + column + " = :mode WHERE id = :id")
                .setParameter("mode", "UNKNOWN")
                .setParameter("id", policy.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(constraint);
    }

    /** Exige indicadores y modos incluso cuando las funcionalidades están desactivadas. */
    @ParameterizedTest
    @ValueSource(strings = {
            "url_filtering_enabled", "url_filtering_mode",
            "download_control_enabled", "download_control_mode"
    })
    void databaseRejectsMissingConfiguration(String column) {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));
        policy.configureUrlFiltering(false, FilterMode.DENYLIST);
        policy.configureDownloadControl(false, FilterMode.DENYLIST);
        entityManager.flush();

        // La columna procede únicamente de los casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE policies SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", policy.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** Comprueba que PostgreSQL rechaza nombres sin contenido. */
    @ParameterizedTest
    @ValueSource(strings = {"", "   ", "\t\n"})
    void databaseRejectsBlankName(String name) {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE policies SET name = :name WHERE id = :id")
                .setParameter("name", name)
                .setParameter("id", policy.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_policies_name_not_blank");
    }

    /** Comprueba que PostgreSQL exige una compañía. */
    @Test
    void databaseRejectsMissingCompany() {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE policies SET company_id = NULL WHERE id = :id")
                .setParameter("id", policy.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("company_id");
    }

    /** Comprueba que la clave foránea rechaza una compañía inexistente. */
    @Test
    void databaseRejectsUnknownCompany() {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE policies SET company_id = :companyId WHERE id = :id")
                .setParameter("companyId", Long.MAX_VALUE)
                .setParameter("id", policy.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_policies_company");
    }

    /** Comprueba que una compañía con políticas no puede eliminarse. */
    @Test
    void databaseRejectsDeletingReferencedCompany() {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "DELETE FROM companies WHERE id = :id")
                .setParameter("id", policy.getCompany().getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_policies_company");
    }

    /** Comprueba que eliminar una política conserva su compañía. */
    @Test
    void deletingPolicyDoesNotDeleteCompany() {
        Policy policy = persistPolicy("Política A", persistCompany("Empresa A"));
        Long id = policy.getId();
        Long companyId = policy.getCompany().getId();

        entityManager.remove(policy);
        entityManager.flush();
        entityManager.clear();

        assertThat(entityManager.find(Policy.class, id)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** Guarda una compañía para las relaciones de la prueba. */
    private Company persistCompany(String name) {
        Company company = new Company(name);
        entityManager.persist(company);
        entityManager.flush();
        return company;
    }

    /** Guarda una política con su configuración inicial. */
    private Policy persistPolicy(String name, Company company) {
        Policy policy = new Policy(name, company);
        entityManager.persist(policy);
        entityManager.flush();
        return policy;
    }
}
