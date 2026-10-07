package com.securebrowser.platform.enrollment;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Locale;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las fechas, unicidad y referencias históricas de enrollment en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class EnrollmentTokenPersistenceTest {

    // Las fechas ya caducadas permiten comprobar también las referencias históricas.
    private static final Instant CREATION = Instant.parse("2026-10-01T08:00:00Z");
    private static final Instant EXPIRATION = CREATION.plusSeconds(86400);
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Permite guardar entidades y comprobar restricciones mediante SQL.
    @PersistenceContext
    private EntityManager entityManager;

    /** Guarda los nueve atributos y conserva las fechas de emisión proporcionadas. */
    @Test
    void persistsPendingTokenAndOriginAssociations() {
        String hash = newTokenHash();
        EnrollmentToken token = persistToken(hash.toUpperCase(Locale.ROOT), CREATION, EXPIRATION);
        Long id = token.getId();
        Long licenseId = token.getLicense().getId();
        Long userId = token.getUser().getId();
        Long policyId = token.getPolicy().getId();
        entityManager.clear();

        EnrollmentToken stored = entityManager.find(EnrollmentToken.class, id);

        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getLicense().getId()).isEqualTo(licenseId);
        assertThat(stored.getUser().getId()).isEqualTo(userId);
        assertThat(stored.getPolicy().getId()).isEqualTo(policyId);
        assertThat(stored.getTokenHash()).isEqualTo(hash);
        assertThat(stored.getCreatedAt()).isEqualTo(CREATION);
        assertThat(stored.getExpiresAt()).isEqualTo(EXPIRATION);
        assertThat(stored.getUsedAt()).isNull();
        assertThat(stored.getRevokedAt()).isNull();
        assertThat(stored.isPendingAt(CREATION)).isTrue();
        assertThat(stored.isPendingAt(EXPIRATION)).isFalse();
    }

    /** Persiste el uso o revocación conservando las referencias, hash y emisión. */
    @ParameterizedTest
    @ValueSource(strings = {"used", "revoked"})
    void lifecycleTransitionPreservesIssueData(String state) {
        EnrollmentToken token = persistToken();
        Long id = token.getId();
        Long licenseId = token.getLicense().getId();
        Long userId = token.getUser().getId();
        Long policyId = token.getPolicy().getId();
        String hash = token.getTokenHash();
        entityManager.clear();

        EnrollmentToken stored = entityManager.find(EnrollmentToken.class, id);
        Instant transition = state.equals("used") ? CREATION.plusSeconds(60) : EXPIRATION.plusSeconds(60);
        if (state.equals("used")) {
            stored.markUsed(transition);
        } else {
            stored.revoke(transition);
        }
        entityManager.flush();
        entityManager.clear();

        EnrollmentToken updated = entityManager.find(EnrollmentToken.class, id);
        assertThat(updated.getLicense().getId()).isEqualTo(licenseId);
        assertThat(updated.getUser().getId()).isEqualTo(userId);
        assertThat(updated.getPolicy().getId()).isEqualTo(policyId);
        assertThat(updated.getTokenHash()).isEqualTo(hash);
        assertThat(updated.getCreatedAt()).isEqualTo(CREATION);
        assertThat(updated.getExpiresAt()).isEqualTo(EXPIRATION);
        assertThat(updated.getUsedAt()).isEqualTo(state.equals("used") ? transition : null);
        assertThat(updated.getRevokedAt()).isEqualTo(state.equals("revoked") ? transition : null);
        assertThat(updated.isPendingAt(CREATION.plusSeconds(60))).isFalse();
    }

    /** Mantiene las comparaciones temporales al guardar instantes con nanosegundos. */
    @Test
    void persistsTimestampsWithoutRoundingUsageIntoExpiry() {
        Instant creation = CREATION.plusNanos(123456789);
        Instant expiration = EXPIRATION.plusNanos(987654321);
        EnrollmentToken token = persistToken(newTokenHash(), creation, expiration);
        token.markUsed(token.getExpiresAt().minusNanos(1));
        entityManager.flush();
        Long id = token.getId();
        entityManager.clear();

        EnrollmentToken stored = entityManager.find(EnrollmentToken.class, id);
        assertThat(stored.getCreatedAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(stored.getExpiresAt()).isEqualTo(EXPIRATION.plusNanos(987654000));
        assertThat(stored.getUsedAt()).isEqualTo(stored.getExpiresAt().minusNanos(1000));
    }

    /** La unicidad del hash es global, incluso entre compañías distintas. */
    @Test
    void databaseRejectsDuplicateHashAcrossCompanies() {
        String hash = newTokenHash();
        persistToken(hash, CREATION, EXPIRATION);

        assertThatThrownBy(() -> persistToken(hash.toUpperCase(Locale.ROOT), CREATION, EXPIRATION))
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_enrollment_tokens_token_hash");
    }

    /** El CHECK exige 64 caracteres hexadecimales ASCII en minúsculas. */
    @ParameterizedTest
    @ValueSource(strings = {"", " ", "secret-original", "AB12", "g"})
    void databaseRejectsInvalidHash(String hash) {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET token_hash = :hash WHERE id = :id")
                .setParameter("hash", hash)
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_hash_format");
    }

    /** Un hash de longitud correcta tampoco admite mayúsculas ni caracteres no hexadecimales. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "g", "ａ"})
    void databaseRejectsInvalidHashCharacters(String symbol) {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET token_hash = :hash WHERE id = :id")
                .setParameter("hash", symbol.repeat(64))
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_hash_format");
    }

    /** La caducidad no puede ser anterior ni igual a la emisión. */
    @ParameterizedTest
    @ValueSource(longs = {-1, 0})
    void databaseRejectsExpiryAtOrBeforeCreation(long offset) {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET expires_at = :expiration WHERE id = :id")
                .setParameter("expiration", CREATION.plusSeconds(offset))
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_expiry");
    }

    /** El consumo exige una fecha dentro del intervalo permitido. */
    @ParameterizedTest
    @ValueSource(longs = {-1, 86400, 86401})
    void databaseRejectsUsageOutsideValidityPeriod(long offset) {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET used_at = :usage WHERE id = :id")
                .setParameter("usage", CREATION.plusSeconds(offset))
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_usage");
    }

    /** La revocación no puede preceder a la emisión. */
    @Test
    void databaseRejectsRevocationBeforeCreation() {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET revoked_at = :revocation WHERE id = :id")
                .setParameter("revocation", CREATION.minusSeconds(1))
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_revocation");
    }

    /** PostgreSQL impide registrar uso y revocación en la misma fila. */
    @Test
    void databaseRejectsSimultaneousUsageAndRevocation() {
        EnrollmentToken token = persistToken();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET used_at = :transition, revoked_at = :transition WHERE id = :id")
                .setParameter("transition", CREATION.plusSeconds(60))
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_enrollment_tokens_lifecycle");
    }

    /** PostgreSQL exige todas las referencias y los datos de emisión. */
    @ParameterizedTest
    @ValueSource(strings = {"license_id", "user_id", "policy_id", "token_hash", "created_at", "expires_at"})
    void databaseRejectsMissingRequiredValue(String column) {
        EnrollmentToken token = persistToken();

        // La columna procede únicamente de los seis casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** Las tres claves foráneas rechazan recursos inexistentes. */
    @ParameterizedTest
    @CsvSource({
            "license_id, fk_enrollment_tokens_license",
            "user_id, fk_enrollment_tokens_user",
            "policy_id, fk_enrollment_tokens_policy"
    })
    void databaseRejectsUnknownOriginReference(String column, String constraint) {
        EnrollmentToken token = persistToken();

        // La columna procede únicamente de los tres casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE enrollment_tokens SET " + column + " = :reference WHERE id = :id")
                .setParameter("reference", Long.MAX_VALUE)
                .setParameter("id", token.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(constraint);
    }

    /** Una referencia histórica bloquea borrados incluso tras uso, revocación o caducidad. */
    @ParameterizedTest
    @CsvSource({
            "licenses, pending, fk_enrollment_tokens_license",
            "licenses, used, fk_enrollment_tokens_license",
            "licenses, revoked, fk_enrollment_tokens_license",
            "users, pending, fk_enrollment_tokens_user",
            "users, used, fk_enrollment_tokens_user",
            "users, revoked, fk_enrollment_tokens_user",
            "policies, pending, fk_enrollment_tokens_policy",
            "policies, used, fk_enrollment_tokens_policy",
            "policies, revoked, fk_enrollment_tokens_policy"
    })
    void databaseRejectsDeletingHistoricalOrigin(String table, String state, String constraint) {
        EnrollmentToken token = persistToken();
        if (state.equals("used")) {
            token.markUsed(CREATION.plusSeconds(60));
        } else if (state.equals("revoked")) {
            token.revoke(EXPIRATION.plusSeconds(60));
        }
        entityManager.flush();

        Long referenceId = switch (table) {
            case "licenses" -> token.getLicense().getId();
            case "users" -> token.getUser().getId();
            case "policies" -> token.getPolicy().getId();
            default -> throw new IllegalArgumentException("Unknown test table");
        };

        // La tabla procede únicamente de los casos fijos anteriores.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "DELETE FROM " + table + " WHERE id = :id")
                .setParameter("id", referenceId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(constraint);
    }

    /** Guarda un enrollment pendiente con un hash distinto para cada prueba. */
    private EnrollmentToken persistToken() {
        return persistToken(newTokenHash(), CREATION, EXPIRATION);
    }

    /** Guarda las referencias dentro de una compañía y después el enrollment. */
    private EnrollmentToken persistToken(String hash, Instant createdAt, Instant expiresAt) {
        Company company = new Company("Empresa de enrollment");
        entityManager.persist(company);
        License license = new License(company, 10);
        entityManager.persist(license);
        User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, UserRole.USER, company);
        entityManager.persist(user);
        Policy policy = new Policy("Política de enrollment", company);
        entityManager.persist(policy);

        EnrollmentToken token = new EnrollmentToken(license, user, policy, hash, createdAt, expiresAt);
        entityManager.persist(token);
        entityManager.flush();
        return token;
    }

    /** Genera un valor hexadecimal ficticio de 64 caracteres para evitar duplicados. */
    private static String newTokenHash() {
        return UUID.randomUUID().toString().replace("-", "").repeat(2);
    }
}
