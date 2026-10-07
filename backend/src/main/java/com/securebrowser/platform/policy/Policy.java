package com.securebrowser.platform.policy;

import java.time.Instant;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.CascadeType;
import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EntityListeners;
import jakarta.persistence.EnumType;
import jakarta.persistence.Enumerated;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.OneToMany;
import jakarta.persistence.Table;

import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

/**
 * Representa la configuración de filtrado de una compañía.
 */
@Entity
@EntityListeners(AuditingEntityListener.class)
@Table(name = "policies")
public class Policy {

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Nombre obligatorio; puede repetirse entre políticas.
    @Column(nullable = false, length = 150)
    private String name;

    // Compañía de origen, conservada durante la vida de la política.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "company_id", nullable = false, updatable = false)
    private Company company;

    // El filtrado de URLs está activado inicialmente.
    @Column(name = "url_filtering_enabled", nullable = false)
    private boolean urlFilteringEnabled = true;

    // El modo sigue siendo obligatorio cuando el filtrado está desactivado.
    @Enumerated(EnumType.STRING)
    @Column(name = "url_filtering_mode", nullable = false, length = 9)
    private FilterMode urlFilteringMode = FilterMode.DENYLIST;

    // Las reglas se gestionan desde esta política y se eliminan con ella.
    @OneToMany(mappedBy = "policy", cascade = CascadeType.ALL, orphanRemoval = true)
    private List<UrlRule> urlRules = new ArrayList<>();

    // El control de descargas está activado inicialmente.
    @Column(name = "download_control_enabled", nullable = false)
    private boolean downloadControlEnabled = true;

    // Todas las reglas de descarga comparten este modo.
    @Enumerated(EnumType.STRING)
    @Column(name = "download_control_mode", nullable = false, length = 9)
    private FilterMode downloadControlMode = FilterMode.DENYLIST;

    // Las reglas de descarga se gestionan y eliminan desde su política.
    @OneToMany(mappedBy = "policy", cascade = CascadeType.ALL, orphanRemoval = true)
    private List<DownloadRule> downloadRules = new ArrayList<>();

    // Fecha de creación asignada por la auditoría.
    @CreatedDate
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Fecha de modificación actualizada por la auditoría.
    @LastModifiedDate
    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    /** Permite a JPA reconstruir la política desde la base de datos. */
    protected Policy() {
    }

    /** Crea una política con nombre y compañía válidos y los modos iniciales. */
    public Policy(String name, Company company) {
        if (company == null) {
            throw new IllegalArgumentException("Company is required");
        }

        this.name = normalizeName(name);
        this.company = company;
    }

    /** Normaliza el nombre y comprueba su contenido y longitud. */
    private static String normalizeName(String name) {
        if (name == null) {
            throw new IllegalArgumentException("Policy name is required");
        }

        String normalized = name.strip();

        if (normalized.isEmpty()) {
            throw new IllegalArgumentException("Policy name must not be blank");
        }

        if (normalized.codePointCount(0, normalized.length()) > 150) {
            throw new IllegalArgumentException("Policy name must not exceed 150 characters");
        }

        return normalized;
    }

    /** Exige un modo incluso si su funcionalidad está desactivada. */
    private static FilterMode validateMode(FilterMode mode) {
        if (mode == null) {
            throw new IllegalArgumentException("Filter mode is required");
        }

        return mode;
    }

    /** Cambia el nombre respetando las restricciones del dominio. */
    public void changeName(String name) {
        this.name = normalizeName(name);
    }

    /** Configura el filtrado de URLs tras validar el modo. */
    public void configureUrlFiltering(boolean enabled, FilterMode mode) {
        this.urlFilteringMode = validateMode(mode);
        this.urlFilteringEnabled = enabled;
    }

    /** Configura el control de descargas tras validar el modo. */
    public void configureDownloadControl(boolean enabled, FilterMode mode) {
        this.downloadControlMode = validateMode(mode);
        this.downloadControlEnabled = enabled;
    }

    /** Añade un dominio único y mantiene los dos lados de la relación. */
    public UrlRule addUrlRule(String domain) {
        UrlRule rule = new UrlRule(this, domain);
        ensureUrlDomainAvailable(rule.getDomain());
        urlRules.add(rule);
        markRulesChanged();
        return rule;
    }

    /** Retira una regla propia; JPA elimina su fila al sincronizar. */
    public void removeUrlRule(UrlRule rule) {
        if (!urlRules.remove(rule)) {
            throw new IllegalArgumentException("URL rule does not belong to this policy");
        }
        markRulesChanged();
    }

    /** Rechaza dominios repetidos antes de modificar una regla o la colección. */
    void ensureUrlDomainAvailable(String domain) {
        if (urlRules.stream().anyMatch(rule -> rule.getDomain().equals(domain))) {
            throw new IllegalArgumentException("Domain already belongs to this policy");
        }
    }

    /** Marca la política como modificada para que la auditoría actúe al guardar. */
    void markRulesChanged() {
        updatedAt = Instant.now();
    }

    /** Devuelve las reglas sin permitir cambios directos en la colección. */
    public List<UrlRule> getUrlRules() {
        return Collections.unmodifiableList(urlRules);
    }

    /** Añade una extensión única y mantiene los dos lados de la relación. */
    public DownloadRule addDownloadRule(String extension) {
        DownloadRule rule = new DownloadRule(this, extension);
        ensureDownloadExtensionAvailable(rule.getExtension());
        downloadRules.add(rule);
        markRulesChanged();
        return rule;
    }

    /** Retira una regla propia; JPA elimina su fila al sincronizar. */
    public void removeDownloadRule(DownloadRule rule) {
        if (!downloadRules.remove(rule)) {
            throw new IllegalArgumentException("Download rule does not belong to this policy");
        }
        markRulesChanged();
    }

    /** Rechaza extensiones repetidas antes de modificar la regla o la colección. */
    void ensureDownloadExtensionAvailable(String extension) {
        if (downloadRules.stream().anyMatch(rule -> rule.getExtension().equals(extension))) {
            throw new IllegalArgumentException("Extension already belongs to this policy");
        }
    }

    /** Devuelve las reglas de descarga sin permitir cambios directos en la lista. */
    public List<DownloadRule> getDownloadRules() {
        return Collections.unmodifiableList(downloadRules);
    }

    /** Devuelve el identificador de la política. */
    public Long getId() {
        return id;
    }

    /** Devuelve el nombre normalizado. */
    public String getName() {
        return name;
    }

    /** Devuelve la compañía propietaria. */
    public Company getCompany() {
        return company;
    }

    /** Indica si está activado el filtrado de URLs. */
    public boolean isUrlFilteringEnabled() {
        return urlFilteringEnabled;
    }

    /** Devuelve el modo de filtrado de URLs. */
    public FilterMode getUrlFilteringMode() {
        return urlFilteringMode;
    }

    /** Indica si está activado el control de descargas. */
    public boolean isDownloadControlEnabled() {
        return downloadControlEnabled;
    }

    /** Devuelve el modo de control de descargas. */
    public FilterMode getDownloadControlMode() {
        return downloadControlMode;
    }

    /** Devuelve el instante de creación. */
    public Instant getCreatedAt() {
        return createdAt;
    }

    /** Devuelve el instante de la última modificación. */
    public Instant getUpdatedAt() {
        return updatedAt;
    }
}
