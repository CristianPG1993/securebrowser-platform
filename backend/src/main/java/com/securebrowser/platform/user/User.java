package com.securebrowser.platform.user;

import java.time.Instant;
import java.util.Locale;
import java.util.regex.Pattern;

import com.securebrowser.platform.company.Company;

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
import jakarta.persistence.Table;

import org.springframework.data.annotation.CreatedDate;
import org.springframework.data.annotation.LastModifiedDate;
import org.springframework.data.jpa.domain.support.AuditingEntityListener;

/**
 * Representa un usuario perteneciente a una compañía.
 */
@Entity
@EntityListeners(AuditingEntityListener.class)
@Table(name = "users")
public class User {

    // Formato de email con parte local y dominio, sin espacios.
    private static final Pattern EMAIL_PATTERN = Pattern.compile(
            "[a-z0-9!#$%&'*+/=?^_`{|}~-]+"
                    + "(?:\\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*"
                    + "@(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\\.)+"
                    + "[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?");

    // Formato de los hashes BCrypt que recibirá la entidad.
    private static final Pattern BCRYPT_PATTERN = Pattern.compile(
            "\\$2[aby]\\$(0[4-9]|[12][0-9]|3[01])\\$[./A-Za-z0-9]{53}");

    // Identificador generado por PostgreSQL.
    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    // Nombre obligatorio del usuario.
    @Column(nullable = false, length = 150)
    private String name;

    // Apellido o apellidos obligatorios.
    @Column(name = "last_name", nullable = false, length = 150)
    private String lastName;

    // Email normalizado y único entre todas las compañías.
    @Column(nullable = false, unique = true, length = 254)
    private String email;

    // Hash BCrypt generado previamente por el backend.
    @Column(name = "password_hash", nullable = false, length = 255)
    private String passwordHash;

    // Rol almacenado como ADMIN o USER.
    @Enumerated(EnumType.STRING)
    @Column(nullable = false, length = 5)
    private UserRole role;

    // Compañía de origen, conservada durante la vida del usuario.
    @ManyToOne(fetch = FetchType.LAZY, optional = false)
    @JoinColumn(name = "company_id", nullable = false, updatable = false)
    private Company company;

    // Fecha de creación asignada por la auditoría.
    @CreatedDate
    @Column(name = "created_at", nullable = false, updatable = false)
    private Instant createdAt;

    // Fecha de modificación actualizada por la auditoría.
    @LastModifiedDate
    @Column(name = "updated_at", nullable = false)
    private Instant updatedAt;

    /** Permite a JPA reconstruir el usuario desde la base de datos. */
    protected User() {
    }

    /** Crea un usuario con datos válidos y un hash ya generado. */
    public User(String name, String lastName, String email,
            String passwordHash, UserRole role, Company company) {

        if (company == null) {
            throw new IllegalArgumentException("Company is required");
        }

        this.name = normalizeName(name, "Name");
        this.lastName = normalizeName(lastName, "Last name");
        this.email = normalizeEmail(email);
        this.passwordHash = validatePasswordHash(passwordHash);
        this.role = validateRole(role);
        this.company = company;
    }

    /** Normaliza nombres y apellidos y comprueba su longitud. */
    private static String normalizeName(String value, String field) {
        if (value == null) {
            throw new IllegalArgumentException(field + " is required");
        }

        String normalized = value.strip();

        if (normalized.isEmpty()) {
            throw new IllegalArgumentException(field + " must not be blank");
        }

        if (normalized.codePointCount(0, normalized.length()) > 150) {
            throw new IllegalArgumentException(
                    field + " must not exceed 150 characters");
        }

        return normalized;
    }

    /** Normaliza el email y valida su formato y longitud. */
    private static String normalizeEmail(String email) {
        if (email == null) {
            throw new IllegalArgumentException("Email is required");
        }

        String normalized = email.strip().toLowerCase(Locale.ROOT);

        if (normalized.length() > 254
                || !EMAIL_PATTERN.matcher(normalized).matches()
                || normalized.indexOf('@') > 64) {
            throw new IllegalArgumentException("Email is invalid");
        }

        return normalized;
    }

    /** Comprueba el formato del hash sin modificarlo ni generarlo. */
    private static String validatePasswordHash(String passwordHash) {
        if (passwordHash == null
                || passwordHash.length() > 255
                || !BCRYPT_PATTERN.matcher(passwordHash).matches()) {
            throw new IllegalArgumentException(
                    "Password hash must be a valid BCrypt hash");
        }

        return passwordHash;
    }

    /** Comprueba que el usuario tenga un rol. */
    private static UserRole validateRole(UserRole role) {
        if (role == null) {
            throw new IllegalArgumentException("Role is required");
        }

        return role;
    }

    /** Cambia el nombre respetando las restricciones del dominio. */
    public void changeName(String name) {
        this.name = normalizeName(name, "Name");
    }

    /** Cambia los apellidos respetando las restricciones del dominio. */
    public void changeLastName(String lastName) {
        this.lastName = normalizeName(lastName, "Last name");
    }

    /** Cambia el email y aplica su normalización. */
    public void changeEmail(String email) {
        this.email = normalizeEmail(email);
    }

    /** Sustituye el hash por otro BCrypt generado por el backend. */
    public void changePasswordHash(String passwordHash) {
        this.passwordHash = validatePasswordHash(passwordHash);
    }

    /** Cambia el rol tras las comprobaciones del servicio. */
    public void changeRole(UserRole role) {
        this.role = validateRole(role);
    }

    /** Devuelve el identificador del usuario. */
    public Long getId() {
        return id;
    }

    /** Devuelve el nombre. */
    public String getName() {
        return name;
    }

    /** Devuelve los apellidos. */
    public String getLastName() {
        return lastName;
    }

    /** Devuelve el email normalizado. */
    public String getEmail() {
        return email;
    }

    /** Devuelve el hash para comprobar las credenciales en el backend. */
    public String getPasswordHash() {
        return passwordHash;
    }

    /** Devuelve el rol. */
    public UserRole getRole() {
        return role;
    }

    /** Devuelve la compañía del usuario. */
    public Company getCompany() {
        return company;
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