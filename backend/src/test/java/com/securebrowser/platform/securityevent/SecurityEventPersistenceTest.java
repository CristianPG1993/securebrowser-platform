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

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.hibernate.exception.DataException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba el historial de eventos, sus UUID y restricciones en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class SecurityEventPersistenceTest {

    private static final Instant RECEIVED = Instant.parse("2026-10-07T12:00:00Z");

    // Permite recargar eventos y comprobar restricciones con SQL directo.
    @PersistenceContext
    private EntityManager entityManager;

    /** Recupera los siete atributos, incluido el tipo textual y la Company derivada. */
    @ParameterizedTest
    @EnumSource(SecurityEventType.class)
    void persistsEventWithOriginalContentAndDates(SecurityEventType type) {
        Device device = persistDevice();
        UUID uuid = UUID.randomUUID();
        Long deviceId = device.getId();
        Long companyId = device.getCompany().getId();
        SecurityEvent event = new SecurityEvent(uuid, device, type, "  Detalle original\n",
                RECEIVED.minusSeconds(3600).plusNanos(123456789), RECEIVED.plusNanos(987654321));
        entityManager.persist(event);
        entityManager.flush();
        Long id = event.getId();
        entityManager.clear();

        SecurityEvent stored = entityManager.find(SecurityEvent.class, id);
        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getEventUuid()).isEqualTo(uuid);
        assertThat(stored.getDevice().getId()).isEqualTo(deviceId);
        assertThat(stored.getDevice().getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.getType()).isEqualTo(type);
        assertThat(stored.getDetails()).isEqualTo("  Detalle original\n");
        assertThat(stored.getOccurredAt()).isEqualTo(RECEIVED.minusSeconds(3600).plusNanos(123456000));
        assertThat(stored.getReceivedAt()).isEqualTo(RECEIVED.plusNanos(987654000));
        assertThat(entityManager.createNativeQuery("SELECT type FROM security_events WHERE id = :id")
                .setParameter("id", id).getSingleResult()).isEqualTo(type.name());
    }

    /** Las fechas explícitas se conservan al guardar incluso con un reloj adelantado. */
    @ParameterizedTest
    @ValueSource(longs = {-86400, 0, 86400})
    void acceptsOfflineDatesAndClockSkew(long offset) {
        SecurityEvent event = persistEvent(persistDevice(), UUID.randomUUID(), null,
                RECEIVED.plusSeconds(offset));
        Long id = event.getId();
        entityManager.clear();

        SecurityEvent stored = entityManager.find(SecurityEvent.class, id);
        assertThat(stored.getOccurredAt()).isEqualTo(RECEIVED.plusSeconds(offset));
        assertThat(stored.getReceivedAt()).isEqualTo(RECEIVED);
    }

    /** El contenido opcional se conserva sin recortar ni convertir cadenas vacías. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {"   "})
    void persistsOptionalDetails(String details) {
        Long id = persistEvent(persistDevice(), UUID.randomUUID(), details, RECEIVED).getId();
        entityManager.clear();
        assertThat(entityManager.find(SecurityEvent.class, id).getDetails()).isEqualTo(details);
    }

    /** El límite SQL cuenta también caracteres Unicode completos. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "😀"})
    void databaseRespectsDetailsLengthLimit(String symbol) {
        String maximum = symbol.repeat(2000);
        Long id = persistEvent(persistDevice(), UUID.randomUUID(), maximum, RECEIVED).getId();
        entityManager.clear();
        assertThat(entityManager.find(SecurityEvent.class, id).getDetails()).isEqualTo(maximum);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE security_events SET details = :details WHERE id = :id")
                .setParameter("details", maximum + symbol)
                .setParameter("id", id)
                .executeUpdate())
                .isInstanceOf(DataException.class)
                .satisfies(error -> assertThat(((DataException) error).getSQLException().getSQLState())
                        .isEqualTo("22001"));
    }

    /** Un UUID repetido se rechaza en el mismo Device y también entre compañías. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void databaseRejectsDuplicateUuidGlobally(boolean anotherCompany) {
        Device firstDevice = persistDevice();
        UUID uuid = UUID.randomUUID();
        persistEvent(firstDevice, uuid, "Primer contenido", RECEIVED);
        Device secondDevice = anotherCompany ? persistDevice() : firstDevice;

        assertThatThrownBy(() -> persistEvent(secondDevice, uuid, "Otro contenido", RECEIVED))
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_security_events_event_uuid");
    }

    /** El UUID, Device, tipo y ambas fechas son obligatorios también mediante SQL. */
    @ParameterizedTest
    @ValueSource(strings = {"event_uuid", "device_id", "type", "occurred_at", "received_at"})
    void databaseRejectsMissingRequiredValue(String column) {
        SecurityEvent event = persistEvent(persistDevice(), UUID.randomUUID(), null, RECEIVED);

        // La columna procede únicamente de los cinco casos fijos anteriores.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE security_events SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", event.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** El CHECK rechaza valores ajenos al enum y nombres con otras mayúsculas. */
    @ParameterizedTest
    @ValueSource(strings = {"OTHER", "url_blocked", ""})
    void databaseRejectsUnknownType(String type) {
        SecurityEvent event = persistEvent(persistDevice(), UUID.randomUUID(), null, RECEIVED);
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE security_events SET type = :type WHERE id = :id")
                .setParameter("type", type).setParameter("id", event.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_security_events_type");
    }

    /** La clave foránea rechaza una instalación inexistente. */
    @Test
    void databaseRejectsUnknownDevice() {
        SecurityEvent event = persistEvent(persistDevice(), UUID.randomUUID(), null, RECEIVED);
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE security_events SET device_id = :deviceId WHERE id = :id")
                .setParameter("deviceId", Long.MAX_VALUE)
                .setParameter("id", event.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_security_events_device");
    }

    /** El historial impide borrar el Device aunque esté desactivado. */
    @Test
    void databaseProtectsInactiveDeviceWithEvents() {
        Device device = persistDevice();
        device.deactivate();
        persistEvent(device, UUID.randomUUID(), null, RECEIVED);

        assertThatThrownBy(() -> entityManager.createNativeQuery("DELETE FROM devices WHERE id = :id")
                .setParameter("id", device.getId()).executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_security_events_device");
    }

    /** Cambiar o desactivar Device conserva eventos y admite recibir su historial pendiente. */
    @Test
    void deviceChangesPreserveEventsAndAllowOfflineSynchronization() {
        Device device = persistDevice();
        UUID uuid = UUID.randomUUID();
        Long id = persistEvent(device, uuid, "Detalle original", RECEIVED.minusSeconds(3600)).getId();
        Long deviceId = device.getId();
        Policy replacement = new Policy("Política nueva", device.getCompany());
        entityManager.persist(replacement);
        device.assignPolicy(replacement);
        device.changeName("Nombre nuevo");
        device.deactivate();
        SecurityEvent pending = persistEvent(device, UUID.randomUUID(), "Evento pendiente",
                RECEIVED.minusSeconds(7200));
        Long pendingId = pending.getId();
        entityManager.clear();

        SecurityEvent stored = entityManager.find(SecurityEvent.class, id);
        assertThat(stored.getEventUuid()).isEqualTo(uuid);
        assertThat(stored.getDevice().getId()).isEqualTo(deviceId);
        assertThat(stored.getDevice().isActive()).isFalse();
        assertThat(stored.getDevice().getPolicy().getId()).isEqualTo(replacement.getId());
        assertThat(stored.getType()).isEqualTo(SecurityEventType.URL_BLOCKED);
        assertThat(stored.getDetails()).isEqualTo("Detalle original");
        assertThat(stored.getOccurredAt()).isEqualTo(RECEIVED.minusSeconds(3600));
        assertThat(stored.getReceivedAt()).isEqualTo(RECEIVED);
        SecurityEvent synchronizedEvent = entityManager.find(SecurityEvent.class, pendingId);
        assertThat(synchronizedEvent.getDevice().getId()).isEqualTo(deviceId);
        assertThat(synchronizedEvent.getOccurredAt()).isEqualTo(RECEIVED.minusSeconds(7200));
        assertThat(synchronizedEvent.getReceivedAt()).isEqualTo(RECEIVED);
        assertThat(synchronizedEvent.getDevice().isActive()).isFalse();
    }

    /** Guarda un evento con primera recepción conocida para comprobar su conservación. */
    private SecurityEvent persistEvent(Device device, UUID uuid, String details, Instant occurredAt) {
        SecurityEvent event = new SecurityEvent(uuid, device, SecurityEventType.URL_BLOCKED,
                details, occurredAt, RECEIVED);
        entityManager.persist(event);
        entityManager.flush();
        return event;
    }

    /** Guarda una instalación con recursos propios de una compañía de prueba. */
    private Device persistDevice() {
        Company company = new Company("Empresa de eventos");
        entityManager.persist(company);
        User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy",
                UserRole.USER, company);
        entityManager.persist(user);
        License license = new License(company, 10);
        entityManager.persist(license);
        Policy policy = new Policy("Política inicial", company);
        entityManager.persist(policy);
        EnrollmentToken enrollment = new EnrollmentToken(license, user, policy,
                UUID.randomUUID().toString().replace("-", "").repeat(2),
                RECEIVED.minusSeconds(60), RECEIVED.plusSeconds(86400));
        enrollment.markUsed(RECEIVED);
        entityManager.persist(enrollment);
        Device device = new Device(UUID.randomUUID().toString(), null,
                company, user, license, policy, enrollment);
        entityManager.persist(device);
        entityManager.flush();
        return device;
    }
}
