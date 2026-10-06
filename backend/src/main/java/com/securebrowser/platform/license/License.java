package com.securebrowser.platform.license;

import java.time.Instant;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.EntityListeners;
import jakarta.persistence.FetchType;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.JoinColumn;
import jakarta.persistence.ManyToOne;
import jakarta.persistence.Table;

import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

/**
 * Representa la capacidad de instalaciones de una compañía.
 */
@Entity
@EntityListeners(AuditingEntityListener.class)
@Table(name = "licenses")
public class License {

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Compañía de origen, conservada durante la vida de la licencia.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "company_id", nullable = false, updatable = false)
    private Company company;

    // Límite de instalaciones; la ocupación se calcula en los servicios.
    @Column(name = "max_installations", nullable = false)
    private int maxInstallations;

    // Fecha de creación asignada por la auditoría.
    @CreatedDate
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Fecha de modificación actualizada por la auditoría.
    @LastModifiedDate
    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    /** Permite a JPA reconstruir la licencia desde la base de datos. */
    protected License() {
    }

    /** Crea una licencia con compañía y capacidad válidas. */
    public License(Company company, int maxInstallations) {
        if (company == null) {
            throw new IllegalArgumentException("Company is required");
        }

        this.company = company;
        this.maxInstallations = validateMaxInstallations(maxInstallations);
    }

    /** Comprueba que la capacidad permita al menos una instalación. */
    private static int validateMaxInstallations(int maxInstallations) {
        if (maxInstallations < 1) {
            throw new IllegalArgumentException("Max installations must be at least 1");
        }

        return maxInstallations;
    }

    /** Cambia el límite tras las comprobaciones de ocupación del servicio. */
    public void changeMaxInstallations(int maxInstallations) {
        this.maxInstallations = validateMaxInstallations(maxInstallations);
    }

    /** Devuelve el identificador de la licencia. */
    public Long getId() {
        return id;
    }

    /** Devuelve la compañía propietaria. */
    public Company getCompany() {
        return company;
    }

    /** Devuelve el número máximo de instalaciones. */
    public int getMaxInstallations() {
        return maxInstallations;
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
