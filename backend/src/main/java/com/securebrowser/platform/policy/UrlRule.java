package com.securebrowser.platform.policy;

import java.net.IDN;
import java.util.Locale;
import java.util.regex.Pattern;

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

/** Representa un dominio normalizado dentro del filtrado de una política. */
@Entity
@Table(name = "url_rules", uniqueConstraints = @UniqueConstraint(
        name = "uq_url_rules_policy_domain", columnNames = {"policy_id", "domain"}))
public class UrlRule {

    // Cada etiqueta admite entre 1 y 63 caracteres ASCII y guiones interiores.
    private static final Pattern DOMAIN_PATTERN = Pattern.compile(
            "[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
            + "(?:[.][a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*");

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Política de origen; su compañía y su modo se obtienen a través de ella.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "policy_id", nullable = false, updatable = false)
    private Policy policy;

    // Dominio sin esquema, ruta, puerto, comodines ni punto final.
    @Column(nullable = false, length = 253)
    private String domain;

    /** Permite a JPA reconstruir la regla desde la base de datos. */
    protected UrlRule() {
    }

    /** Permite a Policy crear sus reglas manteniendo la colección coherente. */
    UrlRule(Policy policy, String domain) {
        if (policy == null) {
            throw new IllegalArgumentException("Policy is required");
        }
        this.policy = policy;
        this.domain = normalizeDomain(domain);
    }

    /** Convierte dominios internacionales a ASCII y valida el formato DNS. */
    private static String normalizeDomain(String domain) {
        if (domain == null) {
            throw new IllegalArgumentException("Domain is required");
        }

        String normalized = IDN.toASCII(domain.strip(), IDN.USE_STD3_ASCII_RULES)
                .toLowerCase(Locale.ROOT);

        if (normalized.endsWith(".")) {
            normalized = normalized.substring(0, normalized.length() - 1);
        }

        if (normalized.length() > 253 || !DOMAIN_PATTERN.matcher(normalized).matches()) {
            throw new IllegalArgumentException("Domain must be valid and not exceed 253 characters");
        }
        return normalized;
    }

    /** Cambia el dominio sin duplicarlo y marca la política como modificada. */
    public void changeDomain(String domain) {
        String normalized = normalizeDomain(domain);
        if (normalized.equals(this.domain)) {
            return;
        }
        policy.ensureUrlDomainAvailable(normalized);
        this.domain = normalized;
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

    /** Devuelve el dominio normalizado en ASCII. */
    public String getDomain() {
        return domain;
    }
}
