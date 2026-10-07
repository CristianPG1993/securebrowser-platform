package com.securebrowser.platform.device;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.UUID;
import java.util.regex.Pattern;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EntityListeners;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToOne;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;

import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

/** Representa una instalación corporativa y conserva su enrollment de origen. */
@Entity
@EntityListeners(AuditingEntityListener.class)
@Table(name = "devices", uniqueConstraints = {
        @UniqueConstraint(name = "uq_devices_device_identifier", columnNames = "device_identifier"),
        @UniqueConstraint(name = "uq_devices_enrollment_token", columnNames = "enrollment_token_id")
})
public class Device {

    // Exige la representación completa del UUID con sus cuatro guiones.
    private static final Pattern UUID_PATTERN = Pattern.compile(
            "[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}");

    // Identificador interno generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Identificador estable generado por Desktop; no es una credencial.
    @Column(name = "device_identifier", nullable = false, length = 36, updatable = false)
    private String deviceIdentifier;

    // Nombre legible opcional de la instalación.
    @Column(length = 150)
    private String name;

    // Compañía de origen, conservada durante la vida del dispositivo.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "company_id", nullable = false, updatable = false)
    private Company company;

    // Usuario propietario de la instalación.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "user_id", nullable = false, updatable = false)
    private User user;

    // Licencia de origen; los servicios coordinarán su ocupación.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "license_id", nullable = false, updatable = false)
    private License license;

    // Política actual; puede cambiar sin alterar la política del enrollment.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "policy_id", nullable = false)
    private Policy policy;

    // Un enrollment origina como máximo una instalación.
    @OneToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "enrollment_token_id", nullable = false, updatable = false)
    private EnrollmentToken enrollmentToken;

    // Una instalación nueva comienza activa.
    @Column(nullable = false)
    private boolean active = true;

    // Fecha de registro asignada por la auditoría.
    @CreatedDate
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Fecha de modificación actualizada por la auditoría.
    @LastModifiedDate
    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    // Última comunicación conocida, inicialmente ausente.
    @Column(name = "last_seen_at")
    private Instant lastSeenAt;

    /** Permite a JPA reconstruir la instalación desde PostgreSQL. */
    protected Device() {
    }

    /** Crea una instalación activa con identificador y referencias obligatorios. */
    public Device(String deviceIdentifier, String name, Company company, User user,
            License license, Policy policy, EnrollmentToken enrollmentToken) {
        if (company == null || user == null || license == null || policy == null || enrollmentToken == null) {
            throw new IllegalArgumentException("Company, user, license, policy and enrollment token are required");
        }

        this.deviceIdentifier = normalizeDeviceIdentifier(deviceIdentifier);
        this.name = normalizeName(name);
        this.company = company;
        this.user = user;
        this.license = license;
        this.policy = policy;
        this.enrollmentToken = enrollmentToken;
    }

    /** Valida el UUID completo y guarda una representación canónica en minúsculas. */
    private static String normalizeDeviceIdentifier(String deviceIdentifier) {
        if (deviceIdentifier == null || !UUID_PATTERN.matcher(deviceIdentifier).matches()) {
            throw new IllegalArgumentException("Device identifier must be a complete UUID");
        }
        return UUID.fromString(deviceIdentifier).toString();
    }

    /** Normaliza el nombre opcional y limita su longitud a 150 caracteres. */
    private static String normalizeName(String name) {
        if (name == null) {
            return null;
        }
        String normalized = name.strip();
        if (normalized.isEmpty()) {
            return null;
        }
        if (normalized.codePointCount(0, normalized.length()) > 150) {
            throw new IllegalArgumentException("Device name must not exceed 150 characters");
        }
        return normalized;
    }

    /** Cambia o retira el nombre legible de la instalación. */
    public void changeName(String name) {
        this.name = normalizeName(name);
    }

    /** Asigna una política tras las comprobaciones de pertenencia del servicio. */
    public void assignPolicy(Policy policy) {
        if (policy == null) {
            throw new IllegalArgumentException("Policy is required");
        }
        this.policy = policy;
    }

    /** Desactiva la instalación conservando su registro y sus relaciones. */
    public void deactivate() {
        active = false;
    }

    /** Reactiva la instalación tras comprobar la capacidad en el servicio. */
    public void activate() {
        active = true;
    }

    /** Registra la fecha proporcionada por el backend tras una comunicación autenticada. */
    public void recordLastSeen(Instant lastSeenAt) {
        if (lastSeenAt == null) {
            throw new IllegalArgumentException("Last seen at is required");
        }
        this.lastSeenAt = lastSeenAt.truncatedTo(ChronoUnit.MICROS);
    }

    /** Devuelve el identificador interno del backend. */
    public Long getId() {
        return id;
    }

    /** Devuelve el UUID estable de la instalación. */
    public String getDeviceIdentifier() {
        return deviceIdentifier;
    }

    /** Devuelve el nombre legible o null si no se ha asignado. */
    public String getName() {
        return name;
    }

    /** Devuelve la compañía de origen. */
    public Company getCompany() {
        return company;
    }

    /** Devuelve el usuario propietario. */
    public User getUser() {
        return user;
    }

    /** Devuelve la licencia de origen. */
    public License getLicense() {
        return license;
    }

    /** Devuelve la política actual de la instalación. */
    public Policy getPolicy() {
        return policy;
    }

    /** Devuelve el enrollment de origen. */
    public EnrollmentToken getEnrollmentToken() {
        return enrollmentToken;
    }

    /** Indica si la instalación está activa. */
    public boolean isActive() {
        return active;
    }

    /** Devuelve el instante de registro. */
    public Instant getCreatedAt() {
        return createdAt;
    }

    /** Devuelve el instante de la última modificación. */
    public Instant getUpdatedAt() {
        return updatedAt;
    }

    /** Devuelve la fecha de la última comunicación conocida o null. */
    public Instant getLastSeenAt() {
        return lastSeenAt;
    }
}
