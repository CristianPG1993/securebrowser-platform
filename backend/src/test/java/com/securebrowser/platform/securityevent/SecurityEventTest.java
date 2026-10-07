package com.securebrowser.platform.securityevent;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.device.Device;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba el contenido y las fechas de eventos sin acceder a PostgreSQL. */
class SecurityEventTest {

    private static final Instant RECEIVED = Instant.parse("2026-10-07T12:00:00Z");
    private static final UUID EVENT_UUID = UUID.fromString("123e4567-e89b-12d3-a456-426614174000");

    /** Conserva los datos, Company derivada y fechas a precisión de PostgreSQL. */
    @ParameterizedTest
    @EnumSource(SecurityEventType.class)
    void constructorPreservesEventContent(SecurityEventType type) {
        Device device = createDevice();
        SecurityEvent event = new SecurityEvent(EVENT_UUID, device, type,
                "  Dominio bloqueado\n", RECEIVED.minusSeconds(3600).plusNanos(123456789),
                RECEIVED.plusNanos(987654321));

        assertThat(event.getId()).isNull();
        assertThat(event.getEventUuid()).isEqualTo(EVENT_UUID);
        assertThat(event.getDevice()).isSameAs(device);
        assertThat(event.getDevice().getCompany()).isSameAs(device.getCompany());
        assertThat(event.getType()).isEqualTo(type);
        assertThat(event.getDetails()).isEqualTo("  Dominio bloqueado\n");
        assertThat(event.getOccurredAt()).isEqualTo(RECEIVED.minusSeconds(3600).plusNanos(123456000));
        assertThat(event.getReceivedAt()).isEqualTo(RECEIVED.plusNanos(987654000));
    }

    /** El detalle es opcional y se conserva incluso vacío o formado por espacios. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {"   ", "\t\n", "  example.com  "})
    void preservesOptionalDetails(String details) {
        SecurityEvent event = new SecurityEvent(EVENT_UUID, createDevice(),
                SecurityEventType.URL_BLOCKED, details, RECEIVED, RECEIVED);
        assertThat(event.getDetails()).isEqualTo(details);
    }

    /** Acepta 2.000 caracteres simples o Unicode y rechaza uno más. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "😀"})
    void respectsDetailsLengthLimit(String symbol) {
        String maximum = symbol.repeat(2000);
        SecurityEvent event = new SecurityEvent(EVENT_UUID, createDevice(),
                SecurityEventType.DOWNLOAD_BLOCKED, maximum, RECEIVED, RECEIVED);

        assertThat(event.getDetails()).isEqualTo(maximum);
        assertThatThrownBy(() -> new SecurityEvent(EVENT_UUID, createDevice(),
                SecurityEventType.DOWNLOAD_BLOCKED, maximum + symbol, RECEIVED, RECEIVED))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Exige UUID, instalación, tipo y las dos fechas. */
    @ParameterizedTest
    @ValueSource(strings = {"eventUuid", "device", "type", "occurredAt", "receivedAt"})
    void rejectsMissingRequiredValue(String missing) {
        Device device = createDevice();
        assertThatThrownBy(() -> new SecurityEvent(
                missing.equals("eventUuid") ? null : EVENT_UUID,
                missing.equals("device") ? null : device,
                missing.equals("type") ? null : SecurityEventType.URL_BLOCKED,
                null, missing.equals("occurredAt") ? null : RECEIVED,
                missing.equals("receivedAt") ? null : RECEIVED))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Admite ocurrencia anterior, simultánea o posterior, también en un Device inactivo. */
    @ParameterizedTest
    @ValueSource(longs = {-86400, 0, 86400})
    void acceptsClockSkewAndInactiveDevice(long offset) {
        Device device = createDevice();
        device.deactivate();
        SecurityEvent event = new SecurityEvent(EVENT_UUID, device,
                SecurityEventType.POLICY_UPDATED, null, RECEIVED.plusSeconds(offset), RECEIVED);

        assertThat(event.getOccurredAt()).isEqualTo(RECEIVED.plusSeconds(offset));
        assertThat(event.getReceivedAt()).isEqualTo(RECEIVED);
        assertThat(device.isActive()).isFalse();
    }

    /** Crea una instalación con referencias corporativas para las pruebas unitarias. */
    private static Device createDevice() {
        Company company = new Company("Empresa A");
        User user = new User("Ana", "García", "ana@example.com",
                "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy",
                UserRole.USER, company);
        License license = new License(company, 10);
        Policy policy = new Policy("Política A", company);
        EnrollmentToken enrollment = new EnrollmentToken(license, user, policy,
                "ab".repeat(32), RECEIVED.minusSeconds(60), RECEIVED.plusSeconds(86400));
        enrollment.markUsed(RECEIVED);
        return new Device(UUID.randomUUID().toString(), null, company, user, license, policy, enrollment);
    }
}
