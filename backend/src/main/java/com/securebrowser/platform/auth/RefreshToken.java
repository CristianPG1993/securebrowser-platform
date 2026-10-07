package com.securebrowser.platform.auth;

import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Locale;
import java.util.UUID;
import java.util.regex.Pattern;

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

/** Conserva el hash de un refresh y sus fechas de consumo y revocación. */
@Entity
@Table(name = "refresh_tokens", uniqueConstraints = @UniqueConstraint(
        name = "uq_refresh_tokens_token_hash", columnNames = "token_hash"))
public class RefreshToken {

    // SHA-256 se representa mediante 64 caracteres hexadecimales.
    private static final Pattern HASH_PATTERN = Pattern.compile("[0-9a-fA-F]{64}");

    // Identificador interno generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Usuario de origen; la Company se obtiene a través de él.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "user_id", nullable = false, updatable = false)
    private User user;

    // Solo se guarda el hash, nunca el secreto de renovación.
    @Column(name = "token_hash", nullable = false, length = 64, updatable = false)
    private String tokenHash;

    // Grupo creado por el backend en el login y compartido durante la rotación.
    @Column(name = "family_id", nullable = false, updatable = false)
    private UUID familyId;

    // Creación de este registro, proporcionada por el backend.
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Caducidad absoluta del grupo, conservada por el servicio durante la rotación.
    @Column(name = "expires_at", nullable = false, updatable = false)
    private Instant expiresAt;

    // Primer consumo; no se sustituye en intentos posteriores.
    @Column(name = "used_at")
    private Instant usedAt;

    // Primera revocación; también puede registrarse después del consumo.
    @Column(name = "revoked_at")
    private Instant revokedAt;

    /** Permite a JPA reconstruir el registro desde PostgreSQL. */
    protected RefreshToken() {
    }

    /** Crea un refresh con usuario, hash, grupo y fechas obligatorios. */
    public RefreshToken(User user, String tokenHash, UUID familyId, Instant createdAt, Instant expiresAt) {
        if (user == null || familyId == null) {
            throw new IllegalArgumentException("User and family ID are required");
        }
        Instant creation = normalizeTimestamp(createdAt, "Created at");
        Instant expiration = normalizeTimestamp(expiresAt, "Expires at");
        if (!expiration.isAfter(creation)) {
            throw new IllegalArgumentException("Expires at must be after created at");
        }
        this.user = user;
        this.tokenHash = normalizeTokenHash(tokenHash);
        this.familyId = familyId;
        this.createdAt = creation;
        this.expiresAt = expiration;
    }

    /** Valida el hash recibido y conserva su representación en minúsculas. */
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

    /** Registra el primer consumo de un refresh disponible dentro de su vigencia. */
    public void markUsed(Instant usedAt) {
        if (this.usedAt != null || revokedAt != null) {
            throw new IllegalStateException("Refresh token is already used or revoked");
        }
        Instant usage = normalizeTimestamp(usedAt, "Used at");
        if (usage.isBefore(createdAt) || !usage.isBefore(expiresAt)) {
            throw new IllegalArgumentException("Used at must be at or after creation and before expiry");
        }
        this.usedAt = usage;
    }

    /** Registra la primera revocación, incluso si el refresh ya fue consumido o caducó. */
    public void revoke(Instant revokedAt) {
        if (this.revokedAt != null) {
            throw new IllegalStateException("Refresh token is already revoked");
        }
        Instant revocation = normalizeTimestamp(revokedAt, "Revoked at");
        if (revocation.isBefore(createdAt)) {
            throw new IllegalArgumentException("Revoked at must be at or after created at");
        }
        this.revokedAt = revocation;
    }

    /** Deriva la disponibilidad en un instante sin consumir ni modificar el registro. */
    public boolean isUsableAt(Instant instant) {
        if (instant == null) {
            throw new IllegalArgumentException("Instant is required");
        }
        return usedAt == null && revokedAt == null
                && !instant.isBefore(createdAt) && instant.isBefore(expiresAt);
    }

    /** Devuelve el identificador interno del refresh. */
    public Long getId() {
        return id;
    }

    /** Devuelve el usuario de origen. */
    public User getUser() {
        return user;
    }

    /** Devuelve únicamente el hash del secreto. */
    public String getTokenHash() {
        return tokenHash;
    }

    /** Devuelve el grupo de login conservado por este registro. */
    public UUID getFamilyId() {
        return familyId;
    }

    /** Devuelve la fecha de creación de este registro. */
    public Instant getCreatedAt() {
        return createdAt;
    }

    /** Devuelve la caducidad absoluta del grupo. */
    public Instant getExpiresAt() {
        return expiresAt;
    }

    /** Devuelve el primer consumo o null si no se ha utilizado. */
    public Instant getUsedAt() {
        return usedAt;
    }

    /** Devuelve la primera revocación o null si no se ha revocado. */
    public Instant getRevokedAt() {
        return revokedAt;
    }
}
