package com.securebrowser.platform.securityevent;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.UUID;

import com.securebrowser.platform.device.Device;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;

/** Conserva un suceso de seguridad y distingue su ocurrencia de la primera recepción. */
@Entity
@Table(name = "security_events", uniqueConstraints = @UniqueConstraint(
        name = "uq_security_events_event_uuid", columnNames = "event_uuid"))
public class SecurityEvent {

    // Identificador interno generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // UUID generado por Desktop y conservado en los reenvíos.
    @Column(name = "event_uuid", nullable = false, updatable = false)
    private UUID eventUuid;

    // Instalación de origen; la Company se obtiene a través de ella.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "device_id", nullable = false, updatable = false)
    private Device device;

    // Tipo textual del suceso, conservado después del registro.
    @Enumerated(EnumType.STRING)
    @Column(nullable = false, length = 16, updatable = false)
    private SecurityEventType type;

    // Contenido descriptivo opcional, sin normalizaciones que alteren un reenvío.
    @Column(length = 2000, updatable = false)
    private String details;

    // Instante registrado por Desktop, cuyo reloj puede estar desajustado.
    @Column(name = "occurred_at", nullable = false, updatable = false)
    private Instant occurredAt;

    // Primera recepción proporcionada por el backend, sin auditoría que la sustituya.
    @Column(name = "received_at", nullable = false, updatable = false)
    private Instant receivedAt;

    /** Permite a JPA reconstruir el evento desde PostgreSQL. */
    protected SecurityEvent() {
    }

    /** Registra un evento sin exigir un orden entre los relojes de Desktop y backend. */
    public SecurityEvent(UUID eventUuid, Device device, SecurityEventType type,
            String details, Instant occurredAt, Instant receivedAt) {
        if (eventUuid == null || device == null || type == null) {
            throw new IllegalArgumentException("Event UUID, device and type are required");
        }
        if (details != null && details.codePointCount(0, details.length()) > 2000) {
            throw new IllegalArgumentException("Event details must not exceed 2000 characters");
        }
        this.eventUuid = eventUuid;
        this.device = device;
        this.type = type;
        this.details = details;
        this.occurredAt = normalizeTimestamp(occurredAt, "Occurred at");
        this.receivedAt = normalizeTimestamp(receivedAt, "Received at");
    }

    /** Ajusta una fecha obligatoria a la precisión de microsegundos de PostgreSQL. */
    private static Instant normalizeTimestamp(Instant value, String field) {
        if (value == null) {
            throw new IllegalArgumentException(field + " is required");
        }
        return value.truncatedTo(ChronoUnit.MICROS);
    }

    /** Devuelve el identificador interno del evento. */
    public Long getId() {
        return id;
    }

    /** Devuelve el UUID utilizado para reconocer reenvíos. */
    public UUID getEventUuid() {
        return eventUuid;
    }

    /** Devuelve la instalación de origen. */
    public Device getDevice() {
        return device;
    }

    /** Devuelve el tipo registrado. */
    public SecurityEventType getType() {
        return type;
    }

    /** Devuelve el detalle original o null si no se proporcionó. */
    public String getDetails() {
        return details;
    }

    /** Devuelve el instante de ocurrencia comunicado por Desktop. */
    public Instant getOccurredAt() {
        return occurredAt;
    }

    /** Devuelve el instante de primera recepción proporcionado por el backend. */
    public Instant getReceivedAt() {
        return receivedAt;
    }
}
