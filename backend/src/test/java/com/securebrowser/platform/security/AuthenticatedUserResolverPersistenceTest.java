package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Comprueba la resolución de identidad usando usuarios reales de PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
class AuthenticatedUserResolverPersistenceTest {

    // Hash BCrypt público utilizado exclusivamente para los datos de prueba.
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    @Autowired
    private AuthenticatedUserResolver resolver;

    @PersistenceContext
    private EntityManager entityManager;

    @Autowired
    private PlatformTransactionManager transactionManager;

    // Limita la limpieza a las compañías creadas por cada prueba.
    private final List<Long> createdCompanyIds = new ArrayList<>();

    /** Obtiene la identidad, compañía y rol desde los datos persistidos. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void resolvesPersistedIdentityAndRole(UserRole role) {
        User user = persistUser("Empresa de prueba", role);

        AuthenticatedUser result = resolver.resolve(user.getId());

        assertThat(result.userId()).isEqualTo(user.getId());
        assertThat(result.companyId()).isEqualTo(user.getCompany().getId());
        assertThat(result.role()).isEqualTo(role);
    }

    /** Una segunda resolución refleja un cambio de rol ya confirmado. */
    @Test
    void reflectsCommittedRoleChange() {
        User user = persistUser("Empresa de prueba", UserRole.ADMIN);
        AuthenticatedUser before = resolver.resolve(user.getId());

        new TransactionTemplate(transactionManager).executeWithoutResult(status ->
                entityManager.find(User.class, user.getId())
                        .changeRole(UserRole.USER));

        AuthenticatedUser after = resolver.resolve(user.getId());

        assertThat(before.role()).isEqualTo(UserRole.ADMIN);
        assertThat(after.role()).isEqualTo(UserRole.USER);
        assertThat(after.userId()).isEqualTo(before.userId());
        assertThat(after.companyId()).isEqualTo(before.companyId());
    }

    /** Rechaza una identidad cuyo usuario ha sido eliminado en PostgreSQL. */
    @Test
    void rejectsUserDeletedAfterPreviousResolution() {
        User user = persistUser("Empresa de prueba", UserRole.USER);
        resolver.resolve(user.getId());

        new TransactionTemplate(transactionManager).executeWithoutResult(status ->
                entityManager.remove(entityManager.find(User.class, user.getId())));

        assertThatThrownBy(() -> resolver.resolve(user.getId()))
                .isInstanceOf(BadCredentialsException.class)
                .hasMessage("Invalid bearer token");
    }

    /** Conserva la compañía correspondiente a cada usuario resuelto. */
    @Test
    void resolvesEachUsersOwnCompany() {
        User userA = persistUser("Empresa A", UserRole.ADMIN);
        User userB = persistUser("Empresa B", UserRole.ADMIN);

        AuthenticatedUser resultA = resolver.resolve(userA.getId());
        AuthenticatedUser resultB = resolver.resolve(userB.getId());

        assertThat(resultA.companyId()).isEqualTo(userA.getCompany().getId());
        assertThat(resultB.companyId()).isEqualTo(userB.getCompany().getId());
        assertThat(resultA.companyId()).isNotEqualTo(resultB.companyId());
        assertThat(resultA.userId()).isNotEqualTo(resultB.userId());
    }

    /** Crea los datos en una transacción y conserva un ADMIN adicional. */
    private User persistUser(String companyName, UserRole role) {
        return Objects.requireNonNull(
                new TransactionTemplate(transactionManager).execute(status -> {
                    Company company = new Company(companyName);
                    entityManager.persist(company);
                    createdCompanyIds.add(company.getId());

                    entityManager.persist(newUser(UserRole.ADMIN, company));
                    User user = newUser(role, company);
                    entityManager.persist(user);
                    entityManager.flush();
                    return user;
                }));
    }

    /** Asigna un email único a cada usuario de prueba. */
    private User newUser(UserRole role, Company company) {
        return new User(
                "Ana", "García",
                UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, role, company);
    }

    /** Elimina únicamente los usuarios y compañías creados por la prueba. */
    @AfterEach
    void cleanUp() {
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
