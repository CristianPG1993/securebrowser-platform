package com.securebrowser.platform.policy;

import java.util.Locale;

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

/** Representa una extensión normalizada dentro del control de descargas. */
@Entity
@Table(name = "download_rules", uniqueConstraints = @UniqueConstraint(
        name = "uq_download_rules_policy_extension", columnNames = {"policy_id", "extension"}))
public class DownloadRule {

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Política de origen; su compañía y modo se obtienen a través de ella.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "policy_id", nullable = false, updatable = false)
    private Policy policy;

    // Extensión obligatoria, en minúsculas y sin punto inicial.
    @Column(nullable = false, length = 20)
    private String extension;

    /** Permite a JPA reconstruir la regla desde la base de datos. */
    protected DownloadRule() {
    }

    /** Permite a Policy crear reglas manteniendo su colección coherente. */
    DownloadRule(Policy policy, String extension) {
        if (policy == null) {
            throw new IllegalArgumentException("Policy is required");
        }
        this.policy = policy;
        this.extension = normalizeExtension(extension);
    }

    /** Normaliza la extensión y rechaza espacios, rutas y puntos interiores. */
    private static String normalizeExtension(String extension) {
        if (extension == null) {
            throw new IllegalArgumentException("Extension is required");
        }

        String normalized = extension.toLowerCase(Locale.ROOT);
        if (normalized.startsWith(".")) {
            normalized = normalized.substring(1);
        }

        if (normalized.isEmpty()
                || normalized.codePointCount(0, normalized.length()) > 20
                || normalized.codePoints().anyMatch(DownloadRule::isInvalidExtensionCharacter)) {
            throw new IllegalArgumentException("Extension must be valid and not exceed 20 characters");
        }
        return normalized;
    }

    /** Identifica espacios Unicode, controles y separadores de puntos o rutas. */
    private static boolean isInvalidExtensionCharacter(int character) {
        return Character.isWhitespace(character) || Character.isSpaceChar(character)
                || Character.isISOControl(character)
                || character == '.' || character == '/' || character == '\\' || character == ':';
    }

    /** Cambia la extensión sin duplicarla y marca su política como modificada. */
    public void changeExtension(String extension) {
        String normalized = normalizeExtension(extension);
        if (normalized.equals(this.extension)) {
            return;
        }
        policy.ensureDownloadExtensionAvailable(normalized);
        this.extension = normalized;
        policy.markRulesChanged();
    }

    /** Devuelve el identificador de la regla. */
    public Long getId() {
        return id;
    }

    /** Devuelve la política de origen. */
    public Policy getPolicy() {
        return policy;
    }

    /** Devuelve la extensión normalizada. */
    public String getExtension() {
        return extension;
    }
}
