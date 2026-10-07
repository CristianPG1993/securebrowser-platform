package com.securebrowser.platform.enrollment;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Locale;
import java.util.regex.Pattern;

import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;
import jakarta.persistence.UniqueConstraint;

/** Conserva el hash y el ciclo de vida de una autorización de enrollment. */
@Entity
@Table(name = "enrollment_tokens", uniqueConstraints = @UniqueConstraint(
        name = "uq_enrollment_tokens_token_hash", columnNames = "token_hash"))
public class EnrollmentToken {

    // El hash recibido debe representar SHA-256 en 64 caracteres hexadecimales.
    private static final Pattern HASH_PATTERN = Pattern.compile("[0-9a-fA-F]{64}");

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Licencia de origen; el servicio coordinará la reserva de capacidad.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "license_id", nullable = false, updatable = false)
    private License license;

    // Usuario destinatario, conservado durante la vida del enrollment.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "user_id", nullable = false, updatable = false)
    private User user;

    // Política inicial que recibirá la instalación; no cambia posteriormente.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "policy_id", nullable = false, updatable = false)
    private Policy policy;

    // Solo se conserva el hash, nunca el secreto entregado al usuario.
    @Column(name = "token_hash", nullable = false, length = 64, updatable = false)
    private String tokenHash;

    // Caducidad fijada al emitir el token.
    @Column(name = "expires_at", nullable = false, updatable = false)
    private Instant expiresAt;

    // Primer consumo; permanece null mientras no se utilice.
    @Column(name = "used_at")
    private Instant usedAt;

    // Revocación; un token utilizado no puede revocarse.
    @Column(name = "revoked_at")
    private Instant revokedAt;

    // Instante de emisión proporcionado por el backend y conservado al guardar.
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    /** Permite a JPA reconstruir el enrollment desde PostgreSQL. */
    protected EnrollmentToken() {
    }

    /** Crea un enrollment con referencias, hash y fechas de emisión válidos. */
    public EnrollmentToken(License license, User user, Policy policy, String tokenHash,
            Instant createdAt, Instant expiresAt) {
        if (license == null || user == null || policy == null) {
            throw new IllegalArgumentException("License, user and policy are required");
        }
        Instant creation = normalizeTimestamp(createdAt, "Created at");
        Instant expiration = normalizeTimestamp(expiresAt, "Expires at");
        if (!expiration.isAfter(creation)) {
            throw new IllegalArgumentException("Expires at must be after created at");
        }

        this.license = license;
        this.user = user;
        this.policy = policy;
        this.tokenHash = normalizeTokenHash(tokenHash);
        this.createdAt = creation;
        this.expiresAt = expiration;
    }

    /** Valida el hash recibido y unifica su representación en minúsculas. */
    private static String normalizeTokenHash(String tokenHash) {
        if (tokenHash == null || !HASH_PATTERN.matcher(tokenHash).matches()) {
            throw new IllegalArgumentException("Token hash must contain 64 hexadecimal characters");
        }
        return tokenHash.toLowerCase(Locale.ROOT);
    }

    /** Ajusta una fecha obligatoria a la precisión de microsegundos de PostgreSQL. */
    private static Instant normalizeTimestamp(Instant value, String field) {
        if (value == null) {
            throw new IllegalArgumentException(field + " is required");
        }
        return value.truncatedTo(ChronoUnit.MICROS);
    }

    /** Impide repetir un consumo o una revocación y mezclar ambos estados. */
    private void ensurePendingLifecycle() {
        if (usedAt != null || revokedAt != null) {
            throw new IllegalStateException("Enrollment token is already used or revoked");
        }
    }

    /** Registra el primer consumo con una fecha dentro del intervalo de vigencia. */
    public void markUsed(Instant usedAt) {
        ensurePendingLifecycle();
        Instant usage = normalizeTimestamp(usedAt, "Used at");
        if (usage.isBefore(createdAt) || !usage.isBefore(expiresAt)) {
            throw new IllegalArgumentException("Used at must be at or after creation and before expiry");
        }
        this.usedAt = usage;
    }

    /** Registra una revocación sin modificar las referencias ni las fechas de emisión. */
    public void revoke(Instant revokedAt) {
        ensurePendingLifecycle();
        Instant revocation = normalizeTimestamp(revokedAt, "Revoked at");
        if (revocation.isBefore(createdAt)) {
            throw new IllegalArgumentException("Revoked at must be at or after created at");
        }
        this.revokedAt = revocation;
    }

    /** Deriva si está pendiente y vigente en un instante proporcionado por el servicio. */
    public boolean isPendingAt(Instant instant) {
        if (instant == null) {
            throw new IllegalArgumentException("Instant is required");
        }
        return usedAt == null && revokedAt == null
                && !instant.isBefore(createdAt) && instant.isBefore(expiresAt);
    }

    /** Devuelve el identificador del enrollment. */
    public Long getId() {
        return id;
    }

    /** Devuelve la licencia de origen. */
    public License getLicense() {
        return license;
    }

    /** Devuelve el usuario destinatario. */
    public User getUser() {
        return user;
    }

    /** Devuelve la política inicial conservada en el enrollment. */
    public Policy getPolicy() {
        return policy;
    }

    /** Devuelve exclusivamente el hash del token. */
    public String getTokenHash() {
        return tokenHash;
    }

    /** Devuelve la caducidad fijada al emitir el token. */
    public Instant getExpiresAt() {
        return expiresAt;
    }

    /** Devuelve el instante del primer consumo o null si no se ha utilizado. */
    public Instant getUsedAt() {
        return usedAt;
    }

    /** Devuelve la fecha de revocación o null si no se ha revocado. */
    public Instant getRevokedAt() {
        return revokedAt;
    }

    /** Devuelve el instante de emisión. */
    public Instant getCreatedAt() {
        return createdAt;
    }
}
