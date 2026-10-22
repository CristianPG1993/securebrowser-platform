package com.securebrowser.platform.user;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatCode;
import static org.assertj.core.api.Assertions.catchThrowable;

import java.sql.SQLException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.company.CompanyRepository;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.dao.PessimisticLockingFailureException;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Comprueba exclusión y liberación de bloqueos usando transacciones y conexiones distintas. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
class RepositoryLockPersistenceTest {

    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    @Autowired
    private CompanyRepository companyRepository;

    @Autowired
    private UserRepository userRepository;

    @Autowired
    private PlatformTransactionManager transactionManager;

    @PersistenceContext
    private EntityManager entityManager;

    // Estas pruebas confirman transacciones y limpian únicamente sus propias compañías.
    private final List<Long> companyIds = new ArrayList<>();

    /** Limpia los datos confirmados, una vez que los hilos han terminado sus transacciones. */
    @AfterEach
    void removeFixtures() {
        if (companyIds.isEmpty()) {
            return;
        }
        transaction().executeWithoutResult(status -> {
            entityManager.createQuery("delete from User u where u.company.id in :ids")
                    .setParameter("ids", companyIds).executeUpdate();
            entityManager.createQuery("delete from Company c where c.id in :ids")
                    .setParameter("ids", companyIds).executeUpdate();
        });
    }

    /** La fila de Company permanece exclusiva hasta confirmar o revertir la transacción. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void companyLockIsExclusiveAndReleasedAtTransactionEnd(boolean rollback) throws Exception {
        User user = persistFixture();
        Long companyId = user.getCompany().getId();
        verifyExclusiveLock(() -> companyRepository.findByIdForUpdate(companyId).orElseThrow(), rollback);
    }

    /** La fila de User se coordina entre conexiones, también al revertir su transacción. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void userLockIsExclusiveAndReleasedAtTransactionEnd(boolean rollback) throws Exception {
        User user = persistFixture();
        verifyExclusiveLock(() -> userRepository.findByIdAndCompanyIdForUpdate(
                user.getId(), user.getCompany().getId()).orElseThrow(), rollback);
    }

    /** Una compañía inexistente no puede bloquearse como si existiera. */
    @Test
    void missingCompanyReturnsEmpty() {
        transaction().executeWithoutResult(status -> {
            // La consulta devuelve vacío para un id que no existe en este PostgreSQL temporal.
            assertThat(companyRepository.findByIdForUpdate(Long.MAX_VALUE)).isEmpty();
        });
    }

    /** Mantiene el orden Company -> User y verifica rollback de una escritura protegida. */
    @Test
    void locksCompanyBeforeUserAndRollsBackChanges() {
        User user = persistFixture();
        transaction().executeWithoutResult(status -> {
            Company company = companyRepository.findByIdForUpdate(user.getCompany().getId()).orElseThrow();
            User lockedUser = userRepository.findByIdAndCompanyIdForUpdate(
                    user.getId(), company.getId()).orElseThrow();
            lockedUser.changeName("Cambio que debe revertirse");
            entityManager.flush();
            status.setRollbackOnly();
        });

        transaction().executeWithoutResult(status -> {
            companyRepository.findByIdForUpdate(user.getCompany().getId()).orElseThrow();
            User stored = userRepository.findByIdAndCompanyIdForUpdate(
                    user.getId(), user.getCompany().getId()).orElseThrow();
            // El rollback conserva el nombre original y permite adquirir los bloqueos otra vez.
            assertThat(stored.getName()).isEqualTo("Ana");
        });
    }

    /** Mantiene ocupado un bloqueo mientras otra conexión intenta adquirir la misma fila. */
    private void verifyExclusiveLock(Runnable acquireLock, boolean rollback) throws Exception {
        CountDownLatch locked = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);

        try (var executor = Executors.newSingleThreadExecutor()) {
            var owner = executor.submit(() -> transaction().executeWithoutResult(status -> {
                acquireLock.run();
                locked.countDown();
                awaitRelease(release);
                if (rollback) {
                    status.setRollbackOnly();
                }
            }));

            try {
                // La conexión propietaria debe haber adquirido el bloqueo antes de competir.
                assertThat(locked.await(10, TimeUnit.SECONDS)).isTrue();
                long start = System.nanoTime();
                Throwable failure = catchThrowable(() -> transaction().executeWithoutResult(
                        status -> acquireLock.run()));

                // La conexión competidora recibe un rechazo de bloqueo traducido por Spring.
                assertThat(failure).isInstanceOf(PessimisticLockingFailureException.class);
                // PostgreSQL confirma lock_not_available; no es un error de consulta o conexión.
                assertThat(sqlState(failure)).isEqualTo("55P03");
                // NOWAIT debe fallar con rapidez, sin esperar a que el propietario libere la fila.
                assertThat(Duration.ofNanos(System.nanoTime() - start)).isLessThan(Duration.ofSeconds(5));
            } finally {
                release.countDown();
                owner.get(10, TimeUnit.SECONDS);
            }

            // Tras commit o rollback, una nueva transacción puede adquirir ese mismo bloqueo.
            assertThatCode(() -> transaction().executeWithoutResult(status -> acquireLock.run()))
                    .doesNotThrowAnyException();
        }
    }

    /** Los latches coordinan los hilos sin introducir pausas arbitrarias en los tests. */
    private void awaitRelease(CountDownLatch release) {
        try {
            if (!release.await(10, TimeUnit.SECONDS)) {
                throw new IllegalStateException("Timed out waiting for test lock release");
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Lock test interrupted", exception);
        }
    }

    private String sqlState(Throwable failure) {
        for (Throwable cause = failure; cause != null; cause = cause.getCause()) {
            if (cause instanceof SQLException sqlException) {
                return sqlException.getSQLState();
            }
        }
        return null;
    }

    /** Confirma los datos para que sean visibles desde ambas conexiones de la prueba. */
    private User persistFixture() {
        return Objects.requireNonNull(transaction().execute(status -> {
            Company company = new Company("Compañía de la prueba de bloqueos");
            entityManager.persist(company);
            companyIds.add(company.getId());
            User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                    PASSWORD_HASH, UserRole.ADMIN, company);
            entityManager.persist(user);
            entityManager.flush();
            return user;
        }));
    }

    /** Limita también la duración de las transacciones utilizadas por la prueba. */
    private TransactionTemplate transaction() {
        TransactionTemplate template = new TransactionTemplate(transactionManager);
        template.setTimeout(10);
        return template;
    }
}
