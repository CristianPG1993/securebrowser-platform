package com.securebrowser.platform.device;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba el UUID, nombre y cambios de una instalación sin acceder a PostgreSQL. */
class DeviceTest {

    // UUID completo de ejemplo; es distinto del identificador interno del backend.
    private static final String IDENTIFIER = "123e4567-e89b-12d3-a456-426614174000";
    private static final Instant CREATION = Instant.parse("2026-10-07T08:00:00Z");
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    /** Conserva el UUID canónico e inicializa la instalación activa y sin última conexión. */
    @ParameterizedTest
    @CsvSource({
            "123e4567-e89b-12d3-a456-426614174000, 123e4567-e89b-12d3-a456-426614174000",
            "123E4567-E89B-12D3-A456-426614174000, 123e4567-e89b-12d3-a456-426614174000",
            "00000000-0000-0000-0000-000000000000, 00000000-0000-0000-0000-000000000000"
    })
    void constructorUsesCanonicalIdentifierAndInitialState(String input, String expected) {
        Device device = createDevice(input, "  PC  Oficina  ");
        EnrollmentToken enrollment = device.getEnrollmentToken();

        assertThat(device.getId()).isNull();
        assertThat(device.getDeviceIdentifier()).isEqualTo(expected);
        assertThat(device.getName()).isEqualTo("PC  Oficina");
        assertThat(device.getCompany()).isSameAs(enrollment.getLicense().getCompany());
        assertThat(device.getUser()).isSameAs(enrollment.getUser());
        assertThat(device.getLicense()).isSameAs(enrollment.getLicense());
        assertThat(device.getPolicy()).isSameAs(enrollment.getPolicy());
        assertThat(device.isActive()).isTrue();
        assertThat(device.getLastSeenAt()).isNull();
        assertThat(device.getCreatedAt()).isNull();
        assertThat(device.getUpdatedAt()).isNull();
    }

    /** Rechaza identificadores ausentes, abreviados, mal formados o con espacios. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {
            " ", "not-a-uuid", "1-1-1-1-1", "123e4567e89b12d3a456426614174000",
            "123e4567-e89b-12d3-a456-42661417400",
            "g23e4567-e89b-12d3-a456-426614174000",
            " 123e4567-e89b-12d3-a456-426614174000",
            "123e4567-e89b-12d3-a456-426614174000 "
    })
    void rejectsInvalidIdentifier(String identifier) {
        assertThatThrownBy(() -> createDevice(identifier, "PC Oficina"))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Un nombre ausente o vacío se representa como null al crear y modificar. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {"   ", "\t\n", "\u2003"})
    void normalizesMissingNameToNull(String name) {
        Device device = createDevice(IDENTIFIER, name);
        assertThat(device.getName()).isNull();

        device.changeName("  Equipo A  ");
        assertThat(device.getName()).isEqualTo("Equipo A");

        device.changeName(name);
        assertThat(device.getName()).isNull();
    }

    /** Limita el nombre a 150 caracteres simples o Unicode sin sustituir uno válido. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "😀"})
    void respectsNameLengthLimit(String symbol) {
        String maximum = symbol.repeat(150);
        Device device = createDevice(IDENTIFIER, "  " + maximum + "  ");

        assertThat(device.getName()).isEqualTo(maximum);
        assertThatThrownBy(() -> createDevice(IDENTIFIER, maximum + symbol))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> device.changeName(maximum + symbol))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(device.getName()).isEqualTo(maximum);
    }

    /** Exige las cinco asociaciones del modelo. */
    @ParameterizedTest
    @ValueSource(strings = {"company", "user", "license", "policy", "enrollmentToken"})
    void requiresOriginAssociations(String missing) {
        Device valid = createDevice(IDENTIFIER, "PC Oficina");

        assertThatThrownBy(() -> new Device(IDENTIFIER, "PC Oficina",
                missing.equals("company") ? null : valid.getCompany(),
                missing.equals("user") ? null : valid.getUser(),
                missing.equals("license") ? null : valid.getLicense(),
                missing.equals("policy") ? null : valid.getPolicy(),
                missing.equals("enrollmentToken") ? null : valid.getEnrollmentToken()))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Cambia la política actual conservando todas las referencias de origen. */
    @Test
    void assigningPolicyPreservesEnrollmentAndOrigin() {
        Device device = createDevice(IDENTIFIER, "PC Oficina");
        Company company = device.getCompany();
        User user = device.getUser();
        License license = device.getLicense();
        EnrollmentToken enrollment = device.getEnrollmentToken();
        Policy original = enrollment.getPolicy();
        Policy replacement = new Policy("Política B", company);

        device.assignPolicy(replacement);

        assertThat(device.getPolicy()).isSameAs(replacement);
        assertThat(device.getCompany()).isSameAs(company);
        assertThat(device.getUser()).isSameAs(user);
        assertThat(device.getLicense()).isSameAs(license);
        assertThat(device.getEnrollmentToken()).isSameAs(enrollment);
        assertThat(enrollment.getPolicy()).isSameAs(original);
        assertThat(device.getDeviceIdentifier()).isEqualTo(IDENTIFIER);

        assertThatThrownBy(() -> device.assignPolicy(null))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(device.getPolicy()).isSameAs(replacement);
    }

    /** Desactivar y reactivar conserva la identidad, nombre y enrollment. */
    @Test
    void activityChangesPreserveOriginAndLastSeen() {
        Device device = createDevice(IDENTIFIER, "PC Oficina");
        Company company = device.getCompany();
        User user = device.getUser();
        License license = device.getLicense();
        Policy policy = device.getPolicy();
        EnrollmentToken enrollment = device.getEnrollmentToken();
        device.recordLastSeen(CREATION);

        device.deactivate();
        device.deactivate();
        assertThat(device.isActive()).isFalse();

        device.activate();
        device.activate();

        assertThat(device.isActive()).isTrue();
        assertThat(device.getDeviceIdentifier()).isEqualTo(IDENTIFIER);
        assertThat(device.getName()).isEqualTo("PC Oficina");
        assertThat(device.getCompany()).isSameAs(company);
        assertThat(device.getUser()).isSameAs(user);
        assertThat(device.getLicense()).isSameAs(license);
        assertThat(device.getPolicy()).isSameAs(policy);
        assertThat(device.getEnrollmentToken()).isSameAs(enrollment);
        assertThat(device.getLastSeenAt()).isEqualTo(CREATION);
    }

    /** Registra la comunicación a precisión de PostgreSQL sin reactivar el dispositivo. */
    @Test
    void recordsLastSeenWithoutChangingActivity() {
        Device device = createDevice(IDENTIFIER, "PC Oficina");
        device.deactivate();

        device.recordLastSeen(CREATION.plusNanos(123456789));
        assertThat(device.getLastSeenAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(device.isActive()).isFalse();

        device.recordLastSeen(CREATION.plusSeconds(60));
        assertThat(device.getLastSeenAt()).isEqualTo(CREATION.plusSeconds(60));

        assertThatThrownBy(() -> device.recordLastSeen(null))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(device.getLastSeenAt()).isEqualTo(CREATION.plusSeconds(60));
    }

    /** Construye las referencias de una instalación dentro de una compañía. */
    private static Device createDevice(String identifier, String name) {
        Company company = new Company("Empresa A");
        User user = new User("Ana", "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company);
        License license = new License(company, 10);
        Policy policy = new Policy("Política A", company);
        EnrollmentToken enrollment = new EnrollmentToken(license, user, policy,
                "ab".repeat(32), CREATION.minusSeconds(60), CREATION.plusSeconds(86400));
        enrollment.markUsed(CREATION);
        return new Device(identifier, name, company, user, license, policy, enrollment);
    }
}
