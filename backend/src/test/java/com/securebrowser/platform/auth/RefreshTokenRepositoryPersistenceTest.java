package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.util.List;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba la selección de registros necesarios para revocar todos los grupos de un usuario. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
@Transactional
class RefreshTokenRepositoryPersistenceTest {

    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    private static final Instant NOW = Instant.parse("2026-10-21T08:00:00Z");

    @Autowired
    private RefreshTokenRepository refreshTokenRepository;

    @Autowired
    private UserRepository userRepository;

    @PersistenceContext
    private EntityManager entityManager;

    /** Incluye todos los grupos no revocados y excluye registros ajenos o ya revocados. */
    @Test
    void selectsUnrevokedTokensAcrossAllUserGroups() {
        Fixture fixture = persistFixture();
        entityManager.flush();
        entityManager.clear();

        List<RefreshToken> tokens = refreshTokenRepository
                .findByUser_IdAndRevokedAtIsNullOrderByIdAsc(fixture.user().getId());

        // Devuelve, por id, el token vigente, el consumido y el caducado del mismo usuario.
        assertThat(tokens).extracting(token -> token.getId()).containsExactly(
                fixture.active().getId(), fixture.used().getId(), fixture.expired().getId());
        // Hay dos grupos distintos que deben revocarse al cambiar la contraseña.
        assertThat(tokens).extracting(token -> token.getFamilyId()).containsExactly(
                fixture.active().getFamilyId(), fixture.used().getFamilyId(), fixture.expired().getFamilyId());
    }

    /** Permite aplicar revoke dentro del bloqueo de User sin sobrescribir revocaciones anteriores. */
    @Test
    void selectedTokensCanBeRevokedWithoutChangingOtherRecords() {
        Fixture fixture = persistFixture();
        entityManager.flush();
        entityManager.clear();

        userRepository.findByIdAndCompanyIdForUpdate(
                fixture.user().getId(), fixture.user().getCompany().getId()).orElseThrow();
        refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(fixture.user().getId())
                .forEach(token -> token.revoke(NOW));
        entityManager.flush();
        entityManager.clear();

        for (RefreshToken original : List.of(fixture.active(), fixture.used(), fixture.expired())) {
            RefreshToken stored = entityManager.find(RefreshToken.class, original.getId());
            // Cada registro seleccionado queda revocado con el instante proporcionado.
            assertThat(stored.getRevokedAt()).isEqualTo(NOW);
            // La revocación conserva el consumo previo, incluido el token ya utilizado.
            assertThat(stored.getUsedAt()).isEqualTo(original.getUsedAt());
            // La revocación no cambia la caducidad absoluta de los grupos.
            assertThat(stored.getExpiresAt()).isEqualTo(original.getExpiresAt());
            // La identidad del grupo se conserva.
            assertThat(stored.getFamilyId()).isEqualTo(original.getFamilyId());
        }
        // La fecha de un registro ya revocado no se sustituye.
        assertThat(entityManager.find(RefreshToken.class, fixture.revoked().getId()).getRevokedAt())
                .isEqualTo(NOW.minusSeconds(1800));
        // El refresh de otro usuario conserva su disponibilidad y no queda revocado.
        assertThat(entityManager.find(RefreshToken.class, fixture.other().getId()).getRevokedAt()).isNull();
        // Una segunda selección del usuario no recupera registros que ya se revocaron.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(
                fixture.user().getId())).isEmpty();
    }

    /** Un usuario sin grupos de renovación produce una lista vacía. */
    @Test
    void returnsEmptyWhenUserHasNoTokens() {
        User user = persistUser("Usuario sin tokens");
        entityManager.flush();
        entityManager.clear();

        // La consulta no inventa registros para un usuario que todavía no tiene refresh tokens.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId())).isEmpty();
    }

    /** Prepara grupos vigentes, consumidos, caducados, revocados y pertenecientes a otro usuario. */
    private Fixture persistFixture() {
        User user = persistUser("Empresa A");
        User otherUser = persistUser("Empresa B");
        UUID family = UUID.randomUUID();
        RefreshToken active = persistToken(user, family, NOW.minusSeconds(3600), NOW.plusSeconds(86400));
        RefreshToken used = persistToken(user, family, NOW.minusSeconds(3600), NOW.plusSeconds(86400));
        used.markUsed(NOW.minusSeconds(1800));
        RefreshToken expired = persistToken(user, UUID.randomUUID(),
                NOW.minusSeconds(172800), NOW.minusSeconds(86400));
        RefreshToken revoked = persistToken(user, UUID.randomUUID(),
                NOW.minusSeconds(3600), NOW.plusSeconds(86400));
        revoked.revoke(NOW.minusSeconds(1800));
        RefreshToken other = persistToken(otherUser, UUID.randomUUID(),
                NOW.minusSeconds(3600), NOW.plusSeconds(86400));
        return new Fixture(user, active, used, expired, revoked, other);
    }

    private User persistUser(String companyName) {
        Company company = new Company(companyName);
        entityManager.persist(company);
        User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, UserRole.ADMIN, company);
        entityManager.persist(user);
        return user;
    }

    private RefreshToken persistToken(User user, UUID family, Instant createdAt, Instant expiresAt) {
        String testHash = UUID.randomUUID().toString().replace("-", "").repeat(2);
        RefreshToken token = new RefreshToken(user, testHash, family, createdAt, expiresAt);
        entityManager.persist(token);
        return token;
    }

    private record Fixture(User user, RefreshToken active, RefreshToken used,
            RefreshToken expired, RefreshToken revoked, RefreshToken other) {
    }
}
