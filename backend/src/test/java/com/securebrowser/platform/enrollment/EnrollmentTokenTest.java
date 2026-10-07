package com.securebrowser.platform.enrollment;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Locale;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba el hash, las fechas y las transiciones de EnrollmentToken. */
class EnrollmentTokenTest {

    // Fechas conocidas; ninguna prueba depende del reloj del sistema.
    private static final Instant CREATION = Instant.parse("2026-10-07T08:00:00Z");
    private static final Instant EXPIRATION = CREATION.plusSeconds(86400);
    private static final String TOKEN_HASH = "ab".repeat(32);
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    /** Crea un token pendiente y conserva el hash normalizado y sus fechas. */
    @Test
    void constructorNormalizesHashAndStartsPending() {
        EnrollmentToken token = createToken(TOKEN_HASH.toUpperCase(Locale.ROOT), CREATION, EXPIRATION);

        assertThat(token.getId()).isNull();
        assertThat(token.getTokenHash()).isEqualTo(TOKEN_HASH);
        assertThat(token.getCreatedAt()).isEqualTo(CREATION);
        assertThat(token.getExpiresAt()).isEqualTo(EXPIRATION);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
        assertThat(token.isPendingAt(CREATION)).isTrue();
    }

    /** Exige las tres asociaciones del modelo. */
    @ParameterizedTest
    @ValueSource(strings = {"license", "user", "policy"})
    void requiresOriginAssociations(String missing) {
        EnrollmentToken valid = createToken();

        assertThatThrownBy(() -> new EnrollmentToken(
                missing.equals("license") ? null : valid.getLicense(),
                missing.equals("user") ? null : valid.getUser(),
                missing.equals("policy") ? null : valid.getPolicy(),
                TOKEN_HASH, CREATION, EXPIRATION))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Rechaza hashes ausentes o secretos que no tengan formato SHA-256. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" ", "token-original", "ab12"})
    void rejectsMissingOrInvalidHash(String hash) {
        assertThatThrownBy(() -> createToken(hash, CREATION, EXPIRATION))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** El hash debe tener exactamente 64 caracteres. */
    @ParameterizedTest
    @ValueSource(ints = {63, 65})
    void rejectsIncorrectHashLength(int length) {
        assertThatThrownBy(() -> createToken("a".repeat(length), CREATION, EXPIRATION))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** La longitud correcta no permite caracteres ajenos al hexadecimal ASCII. */
    @ParameterizedTest
    @ValueSource(strings = {"g", "é", " ", "ａ"})
    void rejectsNonHexadecimalHash(String symbol) {
        assertThatThrownBy(() -> createToken(symbol.repeat(64), CREATION, EXPIRATION))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Exige las fechas de emisión y caducidad. */
    @ParameterizedTest
    @ValueSource(strings = {"createdAt", "expiresAt"})
    void requiresIssueDates(String missing) {
        assertThatThrownBy(() -> createToken(TOKEN_HASH,
                missing.equals("createdAt") ? null : CREATION,
                missing.equals("expiresAt") ? null : EXPIRATION))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** La caducidad debe ser estrictamente posterior a la emisión. */
    @ParameterizedTest
    @ValueSource(longs = {-1, 0})
    void rejectsExpiryAtOrBeforeCreation(long offset) {
        assertThatThrownBy(() -> createToken(TOKEN_HASH, CREATION, CREATION.plusSeconds(offset)))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Evita que dos fechas distintas en nanosegundos colapsen al persistir. */
    @Test
    void validatesDatesAtPostgreSqlPrecision() {
        assertThatThrownBy(() -> createToken(TOKEN_HASH, CREATION, CREATION.plusNanos(999)))
                .isInstanceOf(IllegalArgumentException.class);

        EnrollmentToken minimum = createToken(TOKEN_HASH, CREATION, CREATION.plusNanos(1000));
        assertThat(minimum.getExpiresAt()).isEqualTo(CREATION.plusNanos(1000));

        EnrollmentToken token = createToken(TOKEN_HASH, CREATION.plusNanos(123456789),
                EXPIRATION.plusNanos(987654321));
        assertThat(token.getCreatedAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(token.getExpiresAt()).isEqualTo(EXPIRATION.plusNanos(987654000));

        token.markUsed(token.getExpiresAt().minusNanos(1));
        assertThat(token.getUsedAt()).isEqualTo(token.getExpiresAt().minusNanos(1000));
    }

    /** Deriva la vigencia sin modificar filas ni depender del reloj del sistema. */
    @ParameterizedTest
    @CsvSource({"-1, false", "0, true", "60, true", "86399, true", "86400, false", "86401, false"})
    void derivesPendingStateFromDates(long offset, boolean pending) {
        EnrollmentToken token = createToken();

        assertThat(token.isPendingAt(CREATION.plusSeconds(offset))).isEqualTo(pending);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
    }

    /** Registra el uso incluyendo la emisión y excluyendo la caducidad exacta. */
    @ParameterizedTest
    @ValueSource(longs = {0, 60, 86399})
    void recordsUsageWithinValidityPeriod(long offset) {
        EnrollmentToken token = createToken();
        Instant usage = CREATION.plusSeconds(offset);

        token.markUsed(usage);

        assertThat(token.getUsedAt()).isEqualTo(usage);
        assertThat(token.getRevokedAt()).isNull();
        assertThat(token.isPendingAt(usage)).isFalse();
    }

    /** Un consumo fuera del intervalo no modifica el ciclo de vida. */
    @ParameterizedTest
    @ValueSource(longs = {-1, 86400, 86401})
    void rejectsUsageOutsideValidityPeriod(long offset) {
        EnrollmentToken token = createToken();

        assertThatThrownBy(() -> token.markUsed(CREATION.plusSeconds(offset)))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
    }

    /** La revocación permite la emisión y también fechas posteriores a caducar. */
    @ParameterizedTest
    @ValueSource(longs = {0, 60, 86400, 86401})
    void recordsRevocationAtOrAfterCreation(long offset) {
        EnrollmentToken token = createToken();
        Instant revocation = CREATION.plusSeconds(offset);

        token.revoke(revocation);

        assertThat(token.getRevokedAt()).isEqualTo(revocation);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.isPendingAt(CREATION.plusSeconds(60))).isFalse();
    }

    /** Una revocación anterior a la emisión no cambia el token. */
    @Test
    void rejectsRevocationBeforeCreation() {
        EnrollmentToken token = createToken();

        assertThatThrownBy(() -> token.revoke(CREATION.minusSeconds(1)))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(token.getRevokedAt()).isNull();
        assertThat(token.getUsedAt()).isNull();
    }

    /** Las operaciones de ciclo de vida y la consulta requieren una fecha. */
    @Test
    void rejectsMissingLifecycleDates() {
        EnrollmentToken token = createToken();

        assertThatThrownBy(() -> token.markUsed(null)).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> token.revoke(null)).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> token.isPendingAt(null)).isInstanceOf(IllegalArgumentException.class);
        assertThat(token.getUsedAt()).isNull();
        assertThat(token.getRevokedAt()).isNull();
    }

    /** No repite transiciones ni combina uso y revocación; conserva el origen. */
    @ParameterizedTest
    @ValueSource(strings = {"used", "revoked"})
    void rejectsFurtherTransitionsAndPreservesOrigin(String state) {
        EnrollmentToken token = createToken();
        License license = token.getLicense();
        User user = token.getUser();
        Policy policy = token.getPolicy();
        Instant transition = CREATION.plusSeconds(60);
        if (state.equals("used")) {
            token.markUsed(transition);
        } else {
            token.revoke(transition.plusNanos(999));
        }
        Instant usedAt = token.getUsedAt();
        Instant revokedAt = token.getRevokedAt();

        assertThatThrownBy(() -> token.markUsed(transition.plusSeconds(60)))
                .isInstanceOf(IllegalStateException.class);
        assertThatThrownBy(() -> token.revoke(transition.plusSeconds(60)))
                .isInstanceOf(IllegalStateException.class);

        assertThat(token.getUsedAt()).isEqualTo(usedAt);
        assertThat(token.getRevokedAt()).isEqualTo(revokedAt);
        assertThat(token.getLicense()).isSameAs(license);
        assertThat(token.getUser()).isSameAs(user);
        assertThat(token.getPolicy()).isSameAs(policy);
        assertThat(token.getTokenHash()).isEqualTo(TOKEN_HASH);
        assertThat(token.getCreatedAt()).isEqualTo(CREATION);
        assertThat(token.getExpiresAt()).isEqualTo(EXPIRATION);
    }

    /** Crea un enrollment válido con fechas conocidas para las transiciones. */
    private static EnrollmentToken createToken() {
        return createToken(TOKEN_HASH, CREATION, EXPIRATION);
    }

    /** Construye las referencias de origen sin conectar con PostgreSQL. */
    private static EnrollmentToken createToken(String hash, Instant createdAt, Instant expiresAt) {
        Company company = new Company("Empresa A");
        License license = new License(company, 10);
        User user = new User("Ana", "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company);
        Policy policy = new Policy("Política A", company);
        return new EnrollmentToken(license, user, policy, hash, createdAt, expiresAt);
    }
}
