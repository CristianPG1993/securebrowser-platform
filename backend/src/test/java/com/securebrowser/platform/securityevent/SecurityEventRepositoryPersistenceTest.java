package com.securebrowser.platform.securityevent;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.device.Device;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las consultas de eventos sobre un PostgreSQL temporal real. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
@Transactional
class SecurityEventRepositoryPersistenceTest {

    private static final String HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    private static final Instant NOW = Instant.parse("2026-10-23T08:00:00Z");

    @Autowired
    private SecurityEventRepository repository;

    @PersistenceContext
    private EntityManager em;

    @Test
    void findsExactlyTheEventWithTheRequestedUuid() {
        Device device = persistDevice();
        UUID uuid = UUID.randomUUID();
        SecurityEvent expected = new SecurityEvent(uuid, device, SecurityEventType.URL_BLOCKED,
                "Dominio bloqueado", NOW.minusSeconds(60), NOW);
        em.persist(expected);
        em.persist(new SecurityEvent(UUID.randomUUID(), device, SecurityEventType.DOWNLOAD_BLOCKED,
                null, NOW, NOW));
        em.flush();
        em.clear();

        Optional<SecurityEvent> result = repository.findByEventUuid(uuid);

        // El UUID guardado encuentra un evento después de recargar desde PostgreSQL.
        assertThat(result).isPresent();
        // Se recupera el evento de ese UUID y no el otro evento de la misma instalación.
        assertThat(result.orElseThrow().getId()).isEqualTo(expected.getId());
        // Un UUID que todavía no se ha recibido devuelve Optional vacío.
        assertThat(repository.findByEventUuid(UUID.randomUUID())).isEmpty();
    }

    private Device persistDevice() {
        Company company = new Company("Empresa de prueba");
        em.persist(company);
        User owner = new User("Ana", "Demo", UUID.randomUUID() + "@example.com",
                HASH, UserRole.USER, company);
        License license = new License(company, 5);
        Policy policy = new Policy("Política de prueba", company);
        em.persist(owner);
        em.persist(license);
        em.persist(policy);
        EnrollmentToken enrollment = new EnrollmentToken(license, owner, policy,
                UUID.randomUUID().toString().replace("-", "").repeat(2), NOW, NOW.plusSeconds(3600));
        enrollment.markUsed(NOW);
        em.persist(enrollment);
        Device device = new Device(UUID.randomUUID().toString(), "Equipo de prueba",
                company, owner, license, policy, enrollment);
        em.persist(device);
        return device;
    }
}
