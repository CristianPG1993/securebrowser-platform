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

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba el hash, las fechas y las transiciones del refresh sin PostgreSQL. */
class RefreshTokenTest {

    private static final Instant CREATION = Instant.parse("2026-10-07T12:00:00Z");
    private static final Instant EXPIRY = CREATION.plus(7, ChronoUnit.DAYS);
    private static final UUID FAMILY = UUID.fromString("123e4567-e89b-12d3-a456-426614174000");
    private static final String HASH = "ab".repeat(32);

    /** Conserva el origen, normaliza el hash y comienza sin consumo ni revocación. */
    @Test
    void constructorPreservesOriginAndInitialState() {
        User user = createUser();
        RefreshToken token = new RefreshToken(user, HASH.toUpperCase(Locale.ROOT), FAMILY,
                CREATION.plusNanos(123456789), EXPIRY.plusNanos(987654321));

        assertThat(token.getId()).isNull();
        assertThat(token.getUser()).isSameAs(user);
        assertThat(token.getUser().getCompany()).isSameAs(user.getCompany());
        assertThat(token.getTokenHash()).isEqualTo(HASH);
        assertThat(token.getFamilyId()).isEqualTo(FAMILY);
        assertThat(token.getCreatedAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(token.getExpiresAt()).isEqualTo(EXPIRY.plusNanos(987654000));
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
    }

    /** Rechaza hashes ausentes, secretos sin hashear y formatos incorrectos. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" ", "secret", "z", "ab cd"})
    void rejectsInvalidHash(String hash) {
        assertThatThrownBy(() -> new RefreshToken(createUser(), hash, FAMILY, CREATION, EXPIRY))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Exige exactamente 64 caracteres hexadecimales, sin espacios. */
    @ParameterizedTest
    @ValueSource(strings = {"short", "long", "nonHex", "leadingSpace", "trailingSpace"})
    void rejectsMalformedFullHash(String scenario) {
        String hash = switch (scenario) {
            case "short" -> "a".repeat(63);
            case "long" -> "a".repeat(65);
            case "nonHex" -> "z".repeat(64);
            case "leadingSpace" -> " " + HASH;
            case "trailingSpace" -> HASH + " ";
            default -> throw new IllegalArgumentException("Unknown test scenario");
        };
        assertThatThrownBy(() -> new RefreshToken(createUser(), hash, FAMILY, CREATION, EXPIRY))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Exige usuario, grupo y las dos fechas de origen. */
    @ParameterizedTest
    @ValueSource(strings = {"user", "familyId", "createdAt", "expiresAt"})
    void rejectsMissingOrigin(String missing) {
        assertThatThrownBy(() -> new RefreshToken(missing.equals("user") ? null : createUser(),
                HASH, missing.equals("familyId") ? null : FAMILY,
                missing.equals("createdAt") ? null : CREATION,
                missing.equals("expiresAt") ? null : EXPIRY))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** La caducidad debe ser posterior también tras ajustar a microsegundos. */
    @ParameterizedTest
    @ValueSource(longs = {-1000, 0, 999})
    void rejectsExpiryNotAfterCreation(long nanoseconds) {
        assertThatThrownBy(() -> new RefreshToken(createUser(), HASH, FAMILY,
                CREATION, CREATION.plusNanos(nanoseconds)))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Permite consumir desde la creación hasta el último microsegundo de vigencia. */
    @ParameterizedTest
    @ValueSource(strings = {"creation", "beforeExpiry"})
    void acceptsUsageWithinValidity(String boundary) {
        RefreshToken token = createToken();
        Instant usage = boundary.equals("creation") ? CREATION : EXPIRY.minusNanos(1);
        token.markUsed(usage);
        assertThat(token.getUsedAt()).isEqualTo(usage.truncatedTo(ChronoUnit.MICROS));
        assertThat(token.isUsableAt(CREATION.plusSeconds(60))).isFalse();
    }

    /** Rechaza consumos anteriores a la creación o desde la caducidad. */
    @ParameterizedTest
    @ValueSource(strings = {"beforeCreation", "expiry", "afterExpiry"})
    void rejectsUsageOutsideValidity(String boundary) {
        RefreshToken token = createToken();
        Instant usage = switch (boundary) {
            case "beforeCreation" -> CREATION.minusNanos(1);
            case "expiry" -> EXPIRY;
            case "afterExpiry" -> EXPIRY.plusSeconds(1);
            default -> throw new IllegalArgumentException("Unknown test boundary");
        };
        assertThatThrownBy(() -> token.markUsed(usage)).isInstanceOf(IllegalArgumentException.class);
        assertThat(token.getUsedAt()).isNull();
    }

    /** Revoca desde la creación, incluso al caducar o después, sin cambiar el origen. */
    @ParameterizedTest
    @ValueSource(strings = {"creation", "expiry", "afterExpiry"})
    void acceptsRevocationFromCreation(String boundary) {
        RefreshToken token = createToken();
        User user = token.getUser();
        Instant revocation = switch (boundary) {
            case "creation" -> CREATION;
            case "expiry" -> EXPIRY;
            case "afterExpiry" -> EXPIRY.plusSeconds(1);
            default -> throw new IllegalArgumentException("Unknown test boundary");
        };
        token.revoke(revocation);
        assertThat(token.getRevokedAt()).isEqualTo(revocation);
        assertThat(token.getUser()).isSameAs(user);
        assertThat(token.getFamilyId()).isEqualTo(FAMILY);
        assertThat(token.getTokenHash()).isEqualTo(HASH);
        assertThat(token.getCreatedAt()).isEqualTo(CREATION);
        assertThat(token.getExpiresAt()).isEqualTo(EXPIRY);
        assertThat(token.isUsableAt(CREATION)).isFalse();
    }

    /** Un refresh consumido admite revocación y conserva ambas fechas originales. */
    @Test
    void revokesConsumedTokenWithoutReplacingTransitions() {
        RefreshToken token = createToken();
        Instant usage = CREATION.plusSeconds(60).plusNanos(123456789);
        Instant revocation = CREATION.plusSeconds(120).plusNanos(987654321);
        token.markUsed(usage);
        token.revoke(revocation);

        assertThat(token.getUsedAt()).isEqualTo(usage.truncatedTo(ChronoUnit.MICROS));
        assertThat(token.getRevokedAt()).isEqualTo(revocation.truncatedTo(ChronoUnit.MICROS));
        assertThatThrownBy(() -> token.markUsed(CREATION.plusSeconds(180)))
                .isInstanceOf(IllegalStateException.class);
        assertThatThrownBy(() -> token.revoke(CREATION.plusSeconds(180)))
                .isInstanceOf(IllegalStateException.class);
        assertThat(token.getUsedAt()).isEqualTo(usage.truncatedTo(ChronoUnit.MICROS));
        assertThat(token.getRevokedAt()).isEqualTo(revocation.truncatedTo(ChronoUnit.MICROS));
        assertThat(token.isUsableAt(CREATION.plusSeconds(180))).isFalse();
    }

    /** Un primer consumo no se repite aunque el token todavía no esté revocado. */
    @Test
    void rejectsRepeatedConsumption() {
        RefreshToken token = createToken();
        token.markUsed(CREATION);
        assertThatThrownBy(() -> token.markUsed(CREATION.plusSeconds(1)))
                .isInstanceOf(IllegalStateException.class);
        assertThat(token.getUsedAt()).isEqualTo(CREATION);
        assertThat(token.getRevokedAt()).isNull();
    }

    /** Una revocación impide el primer consumo sin introducir usedAt. */
    @Test
    void rejectsConsumptionAfterRevocation() {
        RefreshToken token = createToken();
        token.revoke(CREATION);
        assertThatThrownBy(() -> token.markUsed(CREATION.plusSeconds(1)))
                .isInstanceOf(IllegalStateException.class);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isEqualTo(CREATION);
    }

    /** Rechaza fechas ausentes o una revocación anterior sin alterar el estado. */
    @Test
    void rejectsInvalidTransitionDates() {
        RefreshToken token = createToken();
        assertThatThrownBy(() -> token.markUsed(null)).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> token.revoke(null)).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> token.revoke(CREATION.minusNanos(1)))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
        assertThat(token.isUsableAt(CREATION)).isTrue();
    }

    /** Deriva la vigencia con límites precisos y sin registrar transiciones. */
    @ParameterizedTest
    @CsvSource({"-1, false", "0, true", "1, true", "604799, true", "604800, false", "604801, false"})
    void derivesUsabilityWithoutChangingState(long seconds, boolean expected) {
        RefreshToken token = createToken();
        assertThat(token.isUsableAt(CREATION.plusSeconds(seconds))).isEqualTo(expected);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
        assertThatThrownBy(() -> token.isUsableAt(null)).isInstanceOf(IllegalArgumentException.class);
    }

    /** Crea un refresh de prueba con caducidad absoluta conocida. */
    private static RefreshToken createToken() {
        return new RefreshToken(createUser(), HASH, FAMILY, CREATION, EXPIRY);
    }

    /** Construye el usuario y su compañía sin persistencia. */
    private static User createUser() {
        return new User("Ana", "García", "ana@example.com",
                "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy",
                UserRole.USER, new Company("Empresa A"));
    }
}
