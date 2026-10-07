package com.securebrowser.platform.device;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Locale;
import java.util.Optional;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.hibernate.exception.DataException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las instalaciones, auditoría y referencias históricas en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class DevicePersistenceTest {

    // Fechas conocidas para la auditoría y el enrollment de origen.
    private static final Instant CREATION = Instant.parse("2026-10-07T08:00:00Z");
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Permite guardar entidades y comprobar las restricciones con SQL directo.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite validar la auditoría sin esperas ni depender del reloj del sistema.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Recupera los doce atributos y conserva el nombre y última comunicación opcionales. */
    @Test
    void persistsDeviceWithOriginAndDefaultState() {
        String identifier = UUID.randomUUID().toString();
        Device device = persistDevice(identifier.toUpperCase(Locale.ROOT), null);
        Long id = device.getId();
        Long companyId = device.getCompany().getId();
        Long userId = device.getUser().getId();
        Long licenseId = device.getLicense().getId();
        Long policyId = device.getPolicy().getId();
        Long enrollmentId = device.getEnrollmentToken().getId();
        entityManager.clear();

        Device stored = entityManager.find(Device.class, id);

        assertThat(stored).isNotNull();
        assertThat(stored.getId()).isPositive();
        assertThat(stored.getDeviceIdentifier()).isEqualTo(identifier);
        assertThat(stored.getName()).isNull();
        assertThat(stored.getCompany().getId()).isEqualTo(companyId);
        assertThat(stored.getUser().getId()).isEqualTo(userId);
        assertThat(stored.getLicense().getId()).isEqualTo(licenseId);
        assertThat(stored.getPolicy().getId()).isEqualTo(policyId);
        assertThat(stored.getEnrollmentToken().getId()).isEqualTo(enrollmentId);
        assertThat(stored.isActive()).isTrue();
        assertThat(stored.getCreatedAt()).isNotNull();
        assertThat(stored.getUpdatedAt()).isEqualTo(stored.getCreatedAt());
        assertThat(stored.getLastSeenAt()).isNull();
    }

    /** Cambia nombre, política y actividad conservando el origen y auditando fechas. */
    @Test
    void changesPolicyAndActivityWhilePreservingOriginAndCreationDate() {
        Instant modification = CREATION.plusSeconds(60);
        Instant reactivation = CREATION.plusSeconds(120);

        try {
            auditingHandler.setDateTimeProvider(() -> Optional.of(CREATION));
            Device device = persistDevice();
            Long id = device.getId();
            Long companyId = device.getCompany().getId();
            Long userId = device.getUser().getId();
            Long licenseId = device.getLicense().getId();
            Long initialPolicyId = device.getPolicy().getId();
            Long enrollmentId = device.getEnrollmentToken().getId();
            String identifier = device.getDeviceIdentifier();
            entityManager.clear();

            Device stored = entityManager.find(Device.class, id);
            assertThat(stored.getCreatedAt()).isEqualTo(CREATION);
            assertThat(stored.getUpdatedAt()).isEqualTo(CREATION);

            auditingHandler.setDateTimeProvider(() -> Optional.of(modification));
            Policy replacement = new Policy("Política B", stored.getCompany());
            entityManager.persist(replacement);
            stored.changeName("  Equipo B  ");
            stored.assignPolicy(replacement);
            stored.deactivate();
            stored.recordLastSeen(modification);
            entityManager.flush();
            Long replacementId = replacement.getId();
            entityManager.clear();

            Device updated = entityManager.find(Device.class, id);
            assertThat(updated.getDeviceIdentifier()).isEqualTo(identifier);
            assertThat(updated.getName()).isEqualTo("Equipo B");
            assertThat(updated.getCompany().getId()).isEqualTo(companyId);
            assertThat(updated.getUser().getId()).isEqualTo(userId);
            assertThat(updated.getLicense().getId()).isEqualTo(licenseId);
            assertThat(updated.getPolicy().getId()).isEqualTo(replacementId);
            assertThat(updated.getEnrollmentToken().getId()).isEqualTo(enrollmentId);
            assertThat(updated.getEnrollmentToken().getPolicy().getId()).isEqualTo(initialPolicyId);
            assertThat(updated.isActive()).isFalse();
            assertThat(updated.getCreatedAt()).isEqualTo(CREATION);
            assertThat(updated.getUpdatedAt()).isEqualTo(modification);
            assertThat(updated.getLastSeenAt()).isEqualTo(modification);

            auditingHandler.setDateTimeProvider(() -> Optional.of(reactivation));
            updated.activate();
            entityManager.flush();
            entityManager.clear();

            Device reactivated = entityManager.find(Device.class, id);
            assertThat(reactivated.isActive()).isTrue();
            assertThat(reactivated.getCreatedAt()).isEqualTo(CREATION);
            assertThat(reactivated.getUpdatedAt()).isEqualTo(reactivation);
            assertThat(reactivated.getLastSeenAt()).isEqualTo(modification);
            assertThat(reactivated.getPolicy().getId()).isEqualTo(replacementId);
            assertThat(reactivated.getEnrollmentToken().getId()).isEqualTo(enrollmentId);
        } finally {
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** El identificador no puede repetirse entre enrollments o compañías distintos. */
    @Test
    void databaseRejectsDuplicateIdentifierAcrossCompanies() {
        String identifier = UUID.randomUUID().toString();
        persistDevice(identifier, "Equipo A");

        assertThatThrownBy(() -> persistDevice(identifier.toUpperCase(Locale.ROOT), "Equipo B"))
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_devices_device_identifier");
    }

    /** Un enrollment no origina un segundo Device aunque el primero esté inactivo. */
    @Test
    void databaseRejectsSecondDeviceForSameEnrollment() {
        Device first = persistDevice();
        first.deactivate();
        entityManager.flush();
        Device second = createDevice(first.getEnrollmentToken(), UUID.randomUUID().toString(), "Equipo B");

        assertThatThrownBy(() -> {
            entityManager.persist(second);
            entityManager.flush();
        })
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_devices_enrollment_token");
    }

    /** El CHECK exige el UUID completo y su representación en minúsculas. */
    @ParameterizedTest
    @ValueSource(strings = {
            "", " ", "not-a-uuid", "1-1-1-1-1",
            "123e4567e89b12d3a456426614174000",
            "123E4567-E89B-12D3-A456-426614174000",
            "g23e4567-e89b-12d3-a456-426614174000"
    })
    void databaseRejectsInvalidOrUnnormalizedIdentifier(String identifier) {
        Device device = persistDevice();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE devices SET device_identifier = :identifier WHERE id = :id")
                .setParameter("identifier", identifier)
                .setParameter("id", device.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_devices_device_identifier");
    }

    /** Un nombre vacío debe representarse como null, no como una cadena sin contenido. */
    @ParameterizedTest
    @ValueSource(strings = {"", "   ", "\t\n"})
    void databaseRejectsBlankName(String name) {
        Device device = persistDevice();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE devices SET name = :name WHERE id = :id")
                .setParameter("name", name)
                .setParameter("id", device.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_devices_name_not_blank");
    }

    /** La columna limita los nombres a 150 caracteres simples o Unicode. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "😀"})
    void databaseRespectsNameLengthLimit(String symbol) {
        String maximum = symbol.repeat(150);
        Long id = persistDevice(UUID.randomUUID().toString(), "  " + maximum + "  ").getId();
        entityManager.clear();
        Device stored = entityManager.find(Device.class, id);
        assertThat(stored.getName()).isEqualTo(maximum);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE devices SET name = :name WHERE id = :id")
                .setParameter("name", maximum + symbol)
                .setParameter("id", id)
                .executeUpdate())
                .isInstanceOf(DataException.class)
                .satisfies(error -> assertThat(((DataException) error).getSQLException().getSQLState())
                        .isEqualTo("22001"));
    }

    /** Guarda la última comunicación con microsegundos y admite retirar el nombre. */
    @Test
    void persistsLastSeenWithoutReactivatingDevice() {
        Device device = persistDevice();
        device.deactivate();
        device.changeName("   ");
        device.recordLastSeen(CREATION.plusNanos(123456789));
        entityManager.flush();
        Long id = device.getId();
        entityManager.clear();

        Device stored = entityManager.find(Device.class, id);
        assertThat(stored.getName()).isNull();
        assertThat(stored.getLastSeenAt()).isEqualTo(CREATION.plusNanos(123456000));
        assertThat(stored.isActive()).isFalse();
    }

    /** PostgreSQL exige el identificador, actividad, auditoría y cinco referencias. */
    @ParameterizedTest
    @ValueSource(strings = {
            "device_identifier", "company_id", "user_id", "license_id", "policy_id",
            "enrollment_token_id", "active", "created_at", "updated_at"
    })
    void databaseRejectsMissingRequiredValue(String column) {
        Device device = persistDevice();

        // La columna procede únicamente de los nueve casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE devices SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", device.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** Las cinco claves foráneas rechazan referencias inexistentes. */
    @ParameterizedTest
    @CsvSource({
            "company_id, fk_devices_company",
            "user_id, fk_devices_user",
            "license_id, fk_devices_license",
            "policy_id, fk_devices_policy",
            "enrollment_token_id, fk_devices_enrollment_token"
    })
    void databaseRejectsUnknownReference(String column, String constraint) {
        Device device = persistDevice();

        // La columna procede únicamente de los cinco casos fijos anteriores.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE devices SET " + column + " = :reference WHERE id = :id")
                .setParameter("reference", Long.MAX_VALUE)
                .setParameter("id", device.getId())
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(constraint);
    }

    /** Las referencias históricas siguen impidiendo borrados al desactivar Device. */
    @ParameterizedTest
    @ValueSource(strings = {"companies", "users", "licenses", "policies", "enrollment_tokens"})
    void databaseRejectsDeletingReferencedResourcesForInactiveDevice(String table) {
        Device device = persistDevice();
        device.deactivate();
        entityManager.flush();
        Long referenceId = switch (table) {
            case "companies" -> device.getCompany().getId();
            case "users" -> device.getUser().getId();
            case "licenses" -> device.getLicense().getId();
            case "policies" -> device.getPolicy().getId();
            case "enrollment_tokens" -> device.getEnrollmentToken().getId();
            default -> throw new IllegalArgumentException("Unknown test table");
        };

        // La tabla procede únicamente de los casos fijos anteriores.
        // User, License y Policy también pueden estar protegidos por el enrollment.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "DELETE FROM " + table + " WHERE id = :id")
                .setParameter("id", referenceId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .satisfies(error -> assertThat(((ConstraintViolationException) error)
                        .getSQLException().getSQLState()).isEqualTo("23503"));
    }

    /** Guarda una instalación con un identificador nuevo para cada prueba. */
    private Device persistDevice() {
        return persistDevice(UUID.randomUUID().toString(), "Equipo A");
    }

    /** Guarda un Device con un enrollment y referencias de origen propios. */
    private Device persistDevice(String identifier, String name) {
        Device device = createDevice(persistEnrollment(), identifier, name);
        entityManager.persist(device);
        entityManager.flush();
        return device;
    }

    /** Crea un Device a partir de las referencias de su enrollment de prueba. */
    private static Device createDevice(EnrollmentToken enrollment, String identifier, String name) {
        return new Device(identifier, name, enrollment.getLicense().getCompany(),
                enrollment.getUser(), enrollment.getLicense(), enrollment.getPolicy(), enrollment);
    }

    /** Guarda un enrollment utilizado y sus recursos dentro de la misma compañía. */
    private EnrollmentToken persistEnrollment() {
        Company company = new Company("Empresa de dispositivos");
        entityManager.persist(company);
        User user = new User("Ana", "García", UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, UserRole.USER, company);
        entityManager.persist(user);
        License license = new License(company, 10);
        entityManager.persist(license);
        Policy policy = new Policy("Política inicial", company);
        entityManager.persist(policy);
        String hash = UUID.randomUUID().toString().replace("-", "").repeat(2);
        EnrollmentToken enrollment = new EnrollmentToken(license, user, policy, hash,
                CREATION.minusSeconds(60), CREATION.plusSeconds(86400));
        enrollment.markUsed(CREATION);
        entityManager.persist(enrollment);
        entityManager.flush();
        return enrollment;
    }
}
