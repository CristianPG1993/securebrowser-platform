package com.securebrowser.platform.company;

import java.time.Instant;
import java.util.ArrayList;
import java.util.List;
import java.util.Optional;
import java.util.Objects;
import java.util.UUID;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.transaction.PlatformTransactionManager;



import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;
import com.securebrowser.platform.security.AuthenticatedUser;

import org.springframework.transaction.support.TransactionTemplate;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

/**
 * Comprueba las operaciones y transacciones de CompanyService en PostgreSQL.
 */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
class CompanyServicePersistenceTest {

    // Hash BCrypt de ejemplo utilizado únicamente en los datos de prueba.
    private static final String PASSWORD_HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Servicio real gestionado por Spring.
    @Autowired
    private CompanyService companyService;

    // Permite preparar y consultar los datos de prueba.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite preparar y limpiar datos en transacciones independientes.
    @Autowired
    private PlatformTransactionManager transactionManager;

    // Permite comprobar la auditoría con fechas controladas.
    @Autowired
    private AuditingHandler auditingHandler;

    // Identificadores de las compañías creadas por cada prueba para su limpieza.
    private final List<Long> createdCompanyIds = new ArrayList<>();

    /** Comprueba que cada administrador obtiene su propia compañía. */
    @Test
    void returnsOwnCompanyForEachAdmin() {
        AuthenticatedUser adminA = persistActor("Empresa A", UserRole.ADMIN);
        AuthenticatedUser adminB = persistActor("Empresa B", UserRole.ADMIN);

        Company companyA = companyService.getOwnCompany(adminA);
        Company companyB = companyService.getOwnCompany(adminB);

        assertThat(companyA.getId()).isEqualTo(adminA.companyId());
        assertThat(companyA.getName()).isEqualTo("Empresa A");

        assertThat(companyB.getId()).isEqualTo(adminB.companyId());
        assertThat(companyB.getName()).isEqualTo("Empresa B");

        assertThat(companyA.getId()).isNotEqualTo(companyB.getId());
    }

    /** Comprueba el cambio de nombre, su confirmación y las fechas de auditoría. */
    @Test
    void commitsNameChangeAndUpdatesAuditOnlyForOwnCompany() {
        AuthenticatedUser adminA = persistActor("Empresa A", UserRole.ADMIN);
        AuthenticatedUser adminB = persistActor("Empresa B", UserRole.ADMIN);

        Company beforeA = companyService.getOwnCompany(adminA);
        Company beforeB = companyService.getOwnCompany(adminB);

        Instant createdAt = beforeA.getCreatedAt();
        Instant modifiedAt = beforeA.getUpdatedAt().plusSeconds(60);

        auditingHandler.setDateTimeProvider(() -> Optional.of(modifiedAt));

        Company changed = companyService.changeOwnCompanyName(
                adminA, "  Empresa A nueva  ");

        assertThat(changed.getUpdatedAt()).isEqualTo(modifiedAt);

        Company reloadedA = companyService.getOwnCompany(adminA);
        Company reloadedB = companyService.getOwnCompany(adminB);

        assertThat(reloadedA.getId()).isEqualTo(beforeA.getId());
        assertThat(reloadedA.getName()).isEqualTo("Empresa A nueva");
        assertThat(reloadedA.getCreatedAt()).isEqualTo(createdAt);
        assertThat(reloadedA.getUpdatedAt()).isEqualTo(modifiedAt);

        assertThat(reloadedB.getName()).isEqualTo(beforeB.getName());
        assertThat(reloadedB.getCreatedAt()).isEqualTo(beforeB.getCreatedAt());
        assertThat(reloadedB.getUpdatedAt()).isEqualTo(beforeB.getUpdatedAt());
    }

    /** Comprueba que un nombre inválido no modifica los datos guardados. */
    @Test
    void rejectsInvalidNameWithoutChangingStoredCompany() {
        AuthenticatedUser admin = persistActor("Empresa A", UserRole.ADMIN);
        Company before = companyService.getOwnCompany(admin);

        assertThatThrownBy(() -> companyService.changeOwnCompanyName(admin, "   "))
                .isInstanceOf(IllegalArgumentException.class);

        Company reloaded = companyService.getOwnCompany(admin);

        assertThat(reloaded.getId()).isEqualTo(before.getId());
        assertThat(reloaded.getName()).isEqualTo(before.getName());
        assertThat(reloaded.getCreatedAt()).isEqualTo(before.getCreatedAt());
        assertThat(reloaded.getUpdatedAt()).isEqualTo(before.getUpdatedAt());
    }

    /** Comprueba que el rol USER no puede consultar ni modificar la compañía. */
    @Test
    void rejectsUserAccessWithoutChangingStoredCompany() {
        AuthenticatedUser user = persistActor("Empresa A", UserRole.USER);
        Long companyId = user.companyId();

        TransactionTemplate transactions = new TransactionTemplate(transactionManager);

        Company before = Objects.requireNonNull(
                transactions.execute(status -> entityManager.find(Company.class, companyId)));

        assertThatThrownBy(() -> companyService.getOwnCompany(user))
                .isInstanceOf(AccessDeniedException.class);

        assertThatThrownBy(() -> companyService.changeOwnCompanyName(user, "Empresa modificada"))
                .isInstanceOf(AccessDeniedException.class);

        Company reloaded = Objects.requireNonNull(
                transactions.execute(status -> entityManager.find(Company.class, companyId)));

        assertThat(reloaded.getName()).isEqualTo(before.getName());
        assertThat(reloaded.getCreatedAt()).isEqualTo(before.getCreatedAt());
        assertThat(reloaded.getUpdatedAt()).isEqualTo(before.getUpdatedAt());
    }

    /**
     * Guarda los datos y construye su principal de confianza en la misma transacción.
     */
    private AuthenticatedUser persistActor(String companyName, UserRole role) {
        return Objects.requireNonNull(
                new TransactionTemplate(transactionManager).execute(status -> {
                    Company company = new Company(companyName);
                    entityManager.persist(company);
                    createdCompanyIds.add(company.getId());

                    User user = new User(
                            "Ana", "García",
                            UUID.randomUUID() + "@example.com",
                            PASSWORD_HASH, role, company);

                    entityManager.persist(user);
                    entityManager.flush();
                    return new AuthenticatedUser(user.getId(), company.getId(), user.getRole());
                }));
    }

    /** Elimina los datos creados por la prueba y restaura la auditoría. */
@AfterEach
void cleanUp() {
    auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);

    if (createdCompanyIds.isEmpty()) {
        return;
    }

    new TransactionTemplate(transactionManager).executeWithoutResult(status -> {
        for (Long companyId : createdCompanyIds) {
            entityManager.createNativeQuery(
                    "DELETE FROM users WHERE company_id = :companyId")
                    .setParameter("companyId", companyId)
                    .executeUpdate();

            entityManager.createNativeQuery(
                    "DELETE FROM companies WHERE id = :companyId")
                    .setParameter("companyId", companyId)
                    .executeUpdate();
        }
    });

    createdCompanyIds.clear();
}
}
