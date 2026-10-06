package com.securebrowser.platform.company;

import java.time.Instant;

import jakarta.persistence.Column;
import jakarta.persistence.Entity;
import jakarta.persistence.GeneratedValue;
import jakarta.persistence.GenerationType;
import jakarta.persistence.Id;
import jakarta.persistence.Table;
import jakarta.persistence.EntityListeners;

import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

/**
 * Representa una compañía de SecureBrowser Platform.
 */
@Entity
@EntityListeners(AuditingEntityListener.class)
@Table(name = "companies")
public class Company {

    // Identificador generado por la base de datos.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Nombre obligatorio de la compañía.
    @Column(nullable = false, length = 150)
    private String name;

    // Instante de creación, conservado en las actualizaciones.
    @CreatedDate
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Instante de la última modificación.
    @LastModifiedDate
    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    /**
     * Permite a JPA crear la entidad al recuperarla de la base de datos.
     */
    protected Company() {
    }

    /**
     * Normaliza el nombre y comprueba las restricciones del dominio.
     */
    private static String normalizeName(String name) {
        if (name == null) {
            throw new IllegalArgumentException("Company name is required");
        }

        String normalizedName = name.strip();

        if (normalizedName.isEmpty()) {
            throw new IllegalArgumentException("Company name must not be blank");
        }

        if (normalizedName.codePointCount(0, normalizedName.length()) > 150) {
            throw new IllegalArgumentException("Company name must not exceed 150 characters");
        }

        return normalizedName;
    }

    /**
     * Crea una compañía con un nombre válido y normalizado.
     */
    public Company(String name) {
        this.name = normalizeName(name);
    }

    /**
     * Cambia el nombre de la compañía respetando las reglas del dominio.
     */
    public void changeName(String name) {
        this.name = normalizeName(name);
    }

    /**
     * Devuelve el identificador de la compañía.
     */
    public Long getId() {
        return id;
    }

    /**
     * Devuelve el nombre de la compañía.
     */
    public String getName() {
        return name;
    }

    /**
     * Devuelve el instante de creación de la compañía.
     */
    public Instant getCreatedAt() {
        return createdAt;
    }

    /**
     * Devuelve el instante de la última modificación de la compañía.
     */
    public Instant getUpdatedAt() {
        return updatedAt;
    }
}