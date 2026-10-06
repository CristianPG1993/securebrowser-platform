package com.securebrowser.platform.user;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/**
 * Comprueba la persistencia, auditoría y restricciones de User en PostgreSQL.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class UserPersistenceTest {

    // Hash de prueba sin relación con las credenciales del proyecto.
    private static final String PASSWORD_HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Permite guardar y recuperar entidades.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite utilizar fechas conocidas en la prueba de auditoría.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Comprueba los datos recuperados, el identificador y la auditoría. */
    @Test
    void persistAssignsIdAndAuditDates() {
        Company company = persistCompany("Empresa A");
        User user = persistUser(company);

        Long userId = user.getId();
        Long companyId = company.getId();
        String email = user.getEmail();

        entityManager.clear();

        User stored = entityManager.find(User.class, userId);

        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getName()).isEqualTo("Ana");
        assertThat(stored.getLastName()).isEqualTo("García");
        assertThat(stored.getEmail()).isEqualTo(email);
        assertThat(stored.getPasswordHash()).isEqualTo(PASSWORD_HASH);
        assertThat(stored.getRole()).isEqualTo(UserRole.USER);
        assertThat(stored.getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.getCreatedAt()).isNotNull();
        assertThat(stored.getUpdatedAt()).isEqualTo(stored.getCreatedAt());
    }

    /** Comprueba que una modificación conserva la creación y actualiza la fecha. */
    @Test
    void updatePreservesCreatedAtAndChangesUpdatedAt() {
        Instant creationTime = Instant.parse("2026-10-06T08:00:00Z");
        Instant modificationTime = creationTime.plusSeconds(60);

        try {
            auditingHandler.setDateTimeProvider(
                    () -> Optional.of(creationTime));

            User user = persistUser(persistCompany("Empresa A"));
            Long id = user.getId();

            entityManager.clear();

            User stored = entityManager.find(User.class, id);

            assertThat(stored.getCreatedAt()).isEqualTo(creationTime);
            assertThat(stored.getUpdatedAt()).isEqualTo(creationTime);

            auditingHandler.setDateTimeProvider(
                    () -> Optional.of(modificationTime));

            stored.changeName("Luis");
            stored.changeRole(UserRole.ADMIN);

            entityManager.flush();
            entityManager.clear();

            User updated = entityManager.find(User.class, id);

            assertThat(updated.getName()).isEqualTo("Luis");
            assertThat(updated.getRole()).isEqualTo(UserRole.ADMIN);
            assertThat(updated.getCreatedAt()).isEqualTo(creationTime);
            assertThat(updated.getUpdatedAt()).isEqualTo(modificationTime);
        } finally {
            auditingHandler.setDateTimeProvider(
                    CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** Comprueba la unicidad del email incluso entre compañías diferentes. */
    @Test
    void rejectsDuplicateEmailAcrossCompanies() {
        Company firstCompany = persistCompany("Empresa A");
        Company secondCompany = persistCompany("Empresa B");

        User firstUser = persistUser(firstCompany);

        User duplicate = new User(
                "Luis", "Pérez",
                "  " + firstUser.getEmail().toUpperCase(Locale.ROOT) + "  ",
                PASSWORD_HASH, UserRole.USER, secondCompany);

        assertThatThrownBy(() -> {
            entityManager.persist(duplicate);
            entityManager.flush();
        })
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_users_email");
    }

    /** Comprueba que PostgreSQL rechaza un usuario sin compañía. */
    @Test
    void databaseRejectsMissingCompany() {
        User user = persistUser(persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE users SET company_id = NULL WHERE id = :id")
                .setParameter("id", user.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("company_id");
    }

    /** Comprueba que la clave foránea rechaza una compañía inexistente. */
    @Test
    void databaseRejectsUnknownCompany() {
        User user = persistUser(persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE users SET company_id = :companyId WHERE id = :id")
                .setParameter("companyId", Long.MAX_VALUE)
                .setParameter("id", user.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_users_company");
    }

    /** Comprueba el CHECK de roles evitando la validación del enum Java. */
    @Test
    void databaseRejectsUnknownRole() {
        User user = persistUser(persistCompany("Empresa A"));

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE users SET role = :role WHERE id = :id")
                .setParameter("role", "OTHER")
                .setParameter("id", user.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_users_role");
    }

    /** Comprueba que una compañía con usuarios no puede eliminarse. */
    @Test
    void databaseRejectsDeletingReferencedCompany() {
        Company company = persistCompany("Empresa A");
        persistUser(company);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "DELETE FROM companies WHERE id = :id")
                .setParameter("id", company.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_users_company");
    }

    /** Comprueba que eliminar un usuario conserva su compañía. */
    @Test
    void deletingUserDoesNotDeleteCompany() {
        Company company = persistCompany("Empresa A");
        User user = persistUser(company);

        Long companyId = company.getId();
        Long userId = user.getId();

        entityManager.remove(user);
        entityManager.flush();
        entityManager.clear();

        assertThat(entityManager.find(User.class, userId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** Guarda una compañía para las relaciones de la prueba. */
    private Company persistCompany(String name) {
        Company company = new Company(name);
        entityManager.persist(company);
        entityManager.flush();
        return company;
    }

    /** Guarda un usuario con un email distinto para cada prueba. */
    private User persistUser(Company company) {
        User user = new User(
                "  Ana  ", "  García  ",
                UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, UserRole.USER, company);

        entityManager.persist(user);
        entityManager.flush();
        return user;
    }
}