package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Locale;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba hashes, grupos, transiciones y borrado en cascada en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class RefreshTokenPersistenceTest {

    private static final Instant CREATION = Instant.parse("2026-10-07T12:00:00Z");
    private static final Instant EXPIRY = CREATION.plus(7, ChronoUnit.DAYS);

    // Permite recargar registros y comprobar restricciones mediante SQL directo.
    @PersistenceContext
    private EntityManager entityManager;

    /** Recupera los ocho atributos con UUID nativo, hash canónico y fechas explícitas. */
    @Test
    void persistsRefreshWithOriginAndInitialState() {
        User user = persistUser();
        Long userId = user.getId();
        Long companyId = user.getCompany().getId();
        UUID family = UUID.randomUUID();
        String hash = newHash();
        RefreshToken token = new RefreshToken(user, hash.toUpperCase(Locale.ROOT), family,
                CREATION.plusNanos(123456789), EXPIRY.plusNanos(987654321));
        entityManager.persist(token);
        entityManager.flush();
        Long id = token.getId();
        entityManager.clear();

        RefreshToken stored = entityManager.find(RefreshToken.class, id);
        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getUser().getId()).isEqualTo(userId);
        assertThat(stored.getUser().getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.getTokenHash()).isEqualTo(hash);
        assertThat(stored.getFamilyId()).isEqualTo(family);
        assertThat(stored.getCreatedAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(stored.getExpiresAt()).isEqualTo(EXPIRY.plusNanos(987654000));
        assertThat(stored.getUsedAt()).isNull();
        assertThat(stored.getRevokedAt()).isNull();
        assertThat(stored.isUsableAt(CREATION.plusSeconds(1))).isTrue();
    }

    /** Guarda consumo y revocación simultáneamente conservando el origen y sus fechas. */
    @Test
    void persistsUsageAndRevocationWithoutChangingOrigin() {
        RefreshToken token = persistToken();
        Long id = token.getId();
        Long userId = token.getUser().getId();
        String hash = token.getTokenHash();
        UUID family = token.getFamilyId();
        Instant usage = CREATION.plusSeconds(60).plusNanos(123456789);
        Instant revocation = EXPIRY.plusSeconds(60).plusNanos(987654321);
        token.markUsed(usage);
        token.revoke(revocation);
        entityManager.flush();
        entityManager.clear();

        RefreshToken stored = entityManager.find(RefreshToken.class, id);
        assertThat(stored.getUser().getId()).isEqualTo(userId);
        assertThat(stored.getTokenHash()).isEqualTo(hash);
        assertThat(stored.getFamilyId()).isEqualTo(family);
        assertThat(stored.getCreatedAt()).isEqualTo(CREATION);
        assertThat(stored.getExpiresAt()).isEqualTo(EXPIRY);
        assertThat(stored.getUsedAt()).isEqualTo(usage.truncatedTo(ChronoUnit.MICROS));
        assertThat(stored.getRevokedAt()).isEqualTo(revocation.truncatedTo(ChronoUnit.MICROS));
        assertThat(stored.isUsableAt(CREATION.plusSeconds(120))).isFalse();
    }

    /** Permite varios registros del mismo grupo sin ampliar su caducidad original. */
    @Test
    void persistsTokensWithSharedFamilyAndExpiry() {
        RefreshToken first = persistToken();
        User user = first.getUser();
        UUID family = first.getFamilyId();
        Instant renewal = CREATION.plusSeconds(60);
        first.markUsed(renewal);

        // Los valores se proporcionan expresamente; la rotación transaccional será del servicio.
        RefreshToken second = persistToken(user, newHash(), family, renewal, first.getExpiresAt());
        Long firstId = first.getId();
        Long secondId = second.getId();
        Long userId = user.getId();
        entityManager.clear();

        RefreshToken storedFirst = entityManager.find(RefreshToken.class, firstId);
        RefreshToken storedSecond = entityManager.find(RefreshToken.class, secondId);
        assertThat(storedFirst.getFamilyId()).isEqualTo(family);
        assertThat(storedSecond.getFamilyId()).isEqualTo(family);
        assertThat(storedFirst.getUser().getId()).isEqualTo(userId);
        assertThat(storedSecond.getUser().getId()).isEqualTo(userId);
        assertThat(storedFirst.getExpiresAt()).isEqualTo(EXPIRY);
        assertThat(storedSecond.getExpiresAt()).isEqualTo(EXPIRY);
        assertThat(storedFirst.getCreatedAt()).isEqualTo(CREATION);
        assertThat(storedSecond.getCreatedAt()).isEqualTo(renewal);
        assertThat(storedSecond.getTokenHash()).isNotEqualTo(storedFirst.getTokenHash());
        assertThat(storedFirst.isUsableAt(renewal)).isFalse();
        assertThat(storedSecond.isUsableAt(renewal)).isTrue();
    }

    /** La unicidad del hash se aplica entre grupos y también entre compañías. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void databaseRejectsDuplicateHashGlobally(boolean anotherCompany) {
        RefreshToken first = persistToken();
        User user = anotherCompany ? persistUser() : first.getUser();

        assertThatThrownBy(() -> persistToken(user, first.getTokenHash().toUpperCase(Locale.ROOT),
                UUID.randomUUID(), CREATION, EXPIRY))
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_refresh_tokens_token_hash");
    }

    /** El CHECK exige exactamente 64 caracteres hexadecimales en minúsculas. */
    @ParameterizedTest
    @ValueSource(strings = {"empty", "short", "nonHex", "uppercase", "space"})
    void databaseRejectsInvalidOrUnnormalizedHash(String scenario) {
        RefreshToken token = persistToken();
        String hash = switch (scenario) {
            case "empty" -> "";
            case "short" -> "a".repeat(63);
            case "nonHex" -> "z".repeat(64);
            case "uppercase" -> "AB".repeat(32);
            case "space" -> " ".repeat(64);
            default -> throw new IllegalArgumentException("Unknown test scenario");
        };
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET token_hash = :hash WHERE id = :id")
                .setParameter("hash", hash).setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_refresh_tokens_token_hash");
    }

    /** PostgreSQL exige usuario, hash, grupo y las fechas de origen. */
    @ParameterizedTest
    @ValueSource(strings = {"user_id", "token_hash", "family_id", "created_at", "expires_at"})
    void databaseRejectsMissingRequiredValue(String column) {
        RefreshToken token = persistToken();

        // La columna procede únicamente de los cinco casos fijos anteriores.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** La caducidad SQL debe ser estrictamente posterior a la creación. */
    @ParameterizedTest
    @ValueSource(longs = {-1, 0})
    void databaseRejectsInvalidExpiry(long seconds) {
        RefreshToken token = persistToken();
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET expires_at = :expiration WHERE id = :id")
                .setParameter("expiration", CREATION.plusSeconds(seconds))
                .setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_refresh_tokens_expiry");
    }

    /** El consumo SQL respeta el intervalo con caducidad excluida. */
    @ParameterizedTest
    @ValueSource(strings = {"beforeCreation", "expiry", "afterExpiry"})
    void databaseRejectsInvalidUsage(String boundary) {
        RefreshToken token = persistToken();
        Instant usage = switch (boundary) {
            case "beforeCreation" -> CREATION.minusNanos(1000);
            case "expiry" -> EXPIRY;
            case "afterExpiry" -> EXPIRY.plusSeconds(1);
            default -> throw new IllegalArgumentException("Unknown test boundary");
        };
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET used_at = :usage WHERE id = :id")
                .setParameter("usage", usage).setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_refresh_tokens_usage");
    }

    /** Una revocación SQL no puede preceder a la creación del registro. */
    @Test
    void databaseRejectsRevocationBeforeCreation() {
        RefreshToken token = persistToken();
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET revoked_at = :revocation WHERE id = :id")
                .setParameter("revocation", CREATION.minusNanos(1000))
                .setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_refresh_tokens_revocation");
    }

    /** La clave foránea rechaza un usuario inexistente. */
    @Test
    void databaseRejectsUnknownUser() {
        RefreshToken token = persistToken();
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE refresh_tokens SET user_id = :userId WHERE id = :id")
                .setParameter("userId", Long.MAX_VALUE)
                .setParameter("id", token.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_refresh_tokens_user");
    }

    /** El borrado permitido de User elimina sus grupos sin borrar Company u otros usuarios. */
    @Test
    void deletingUserCascadesOnlyItsRefreshTokens() {
        RefreshToken first = persistToken();
        User user = first.getUser();
        Long companyId = user.getCompany().getId();
        RefreshToken second = persistToken(user, newHash(), first.getFamilyId(),
                CREATION.plusSeconds(60), EXPIRY);
        RefreshToken otherFamily = persistToken(user, newHash(), UUID.randomUUID(), CREATION, EXPIRY);
        RefreshToken otherUser = persistToken();
        first.markUsed(CREATION);
        first.revoke(CREATION.plusSeconds(120));
        second.revoke(CREATION.plusSeconds(120));
        entityManager.flush();
        Long firstId = first.getId();
        Long secondId = second.getId();
        Long otherFamilyId = otherFamily.getId();
        Long otherUserTokenId = otherUser.getId();
        Long otherUserId = otherUser.getUser().getId();
        Long userId = user.getId();

        // Este usuario no tiene Devices ni enrollments; sus refresh no impiden el borrado.
        assertThat(entityManager.createNativeQuery("DELETE FROM users WHERE id = :id")
                .setParameter("id", userId).executeUpdate()).isEqualTo(1);
        entityManager.clear();

        assertThat(entityManager.find(User.class, userId)).isNull();
        assertThat(entityManager.find(RefreshToken.class, firstId)).isNull();
        assertThat(entityManager.find(RefreshToken.class, secondId)).isNull();
        assertThat(entityManager.find(RefreshToken.class, otherFamilyId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
        assertThat(entityManager.find(RefreshToken.class, otherUserTokenId)).isNotNull();
        assertThat(entityManager.find(User.class, otherUserId)).isNotNull();
    }

    /** Guarda un refresh con usuario y grupo propios para cada prueba. */
    private RefreshToken persistToken() {
        return persistToken(persistUser(), newHash(), UUID.randomUUID(), CREATION, EXPIRY);
    }

    /** Guarda las referencias y fechas recibidas sin implementar la rotación del servicio. */
    private RefreshToken persistToken(User user, String hash, UUID family, Instant creation, Instant expiry) {
        RefreshToken token = new RefreshToken(user, hash, family, creation, expiry);
        entityManager.persist(token);
        entityManager.flush();
        return token;
    }

    /** Genera un valor hexadecimal de prueba que no representa un secreto real. */
    private static String newHash() {
        return UUID.randomUUID().toString().replace("-", "").repeat(2);
    }

    /** Guarda un usuario sin enrollments ni Devices y su compañía. */
    private User persistUser() {
        Company company = new Company("Empresa de autenticación");
        entityManager.persist(company);
        User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy",
                UserRole.USER, company);
        entityManager.persist(user);
        entityManager.flush();
        return user;
    }
}
