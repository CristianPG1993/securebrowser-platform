package com.securebrowser.platform.device;

import static org.assertj.core.api.Assertions.assertThat;

import java.time.Instant;
import java.util.Optional;
import java.util.UUID;
import com.securebrowser.platform.company.Company;
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
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba el ámbito de propietario y Company sobre un PostgreSQL temporal real. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
@Transactional
class DeviceRepositoryPersistenceTest {
    private static final String HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";
    @Autowired private DeviceRepository repository;
    @PersistenceContext private EntityManager em;

    @Test
    void findsInstallationOnlyForItsCompanyAndOwner() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        em.persist(companyA);
        em.persist(companyB);
        User ownerA = persistUser(companyA);
        User anotherUserA = persistUser(companyA);
        User ownerB = persistUser(companyB);
        Device deviceA = persistDevice(companyA, ownerA);
        Device deviceB = persistDevice(companyB, ownerB);
        em.flush();
        em.clear();

        Optional<Device> own = repository.findByIdAndCompany_IdAndUser_Id(
                deviceA.getId(), companyA.getId(), ownerA.getId());
        // El propietario puede resolver su instalación dentro de su compañía.
        assertThat(own).isPresent();
        // Se recupera exactamente el Device solicitado.
        assertThat(own.orElseThrow().getId()).isEqualTo(deviceA.getId());
        // Otro usuario de A no puede acceder al Device del propietario A.
        assertThat(repository.findByIdAndCompany_IdAndUser_Id(
                deviceA.getId(), companyA.getId(), anotherUserA.getId())).isEmpty();
        // El propietario correcto con una Company incorrecta tampoco obtiene el Device.
        assertThat(repository.findByIdAndCompany_IdAndUser_Id(
                deviceA.getId(), companyB.getId(), ownerA.getId())).isEmpty();
        // Un propietario de otra compañía no puede resolver la instalación de A.
        assertThat(repository.findByIdAndCompany_IdAndUser_Id(
                deviceA.getId(), companyB.getId(), ownerB.getId())).isEmpty();
        // La instalación de B se encuentra al usar sus propias referencias.
        assertThat(repository.findByIdAndCompany_IdAndUser_Id(
                deviceB.getId(), companyB.getId(), ownerB.getId())).isPresent();
        // Un Device inexistente devuelve el mismo vacío que uno fuera de ámbito.
        assertThat(repository.findByIdAndCompany_IdAndUser_Id(
                Long.MAX_VALUE, companyA.getId(), ownerA.getId())).isEmpty();
    }

    @Test
    void paginatesOnlyCompanyDevicesInIdOrder() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        em.persist(companyA);
        em.persist(companyB);
        User ownerA = persistUser(companyA);
        User ownerB = persistUser(companyB);
        Device first = persistDevice(companyA, ownerA);
        persistDevice(companyB, ownerB);
        Device second = persistDevice(companyA, ownerA);
        Device third = persistDevice(companyA, ownerA);
        em.flush();
        em.clear();

        Page<Device> firstPage = repository.findByCompanyWithFilters(
                companyA.getId(), null, null, PageRequest.of(0, 2));
        Page<Device> secondPage = repository.findByCompanyWithFilters(
                companyA.getId(), null, null, PageRequest.of(1, 2));
        Page<Device> beyondLast = repository.findByCompanyWithFilters(
                companyA.getId(), null, null, PageRequest.of(2, 2));

        // La primera página contiene los dos primeros Device de A, sin incluir el de B intercalado.
        assertThat(firstPage.getContent()).extracting(Device::getId).containsExactly(first.getId(), second.getId());
        // El recuento excluye al Device de B.
        assertThat(firstPage.getTotalElements()).isEqualTo(3L);
        // Tres instalaciones, de dos en dos, ocupan dos páginas.
        assertThat(firstPage.getTotalPages()).isEqualTo(2);
        // La siguiente página conserva el orden ascendente.
        assertThat(secondPage.getContent()).extracting(Device::getId).containsExactly(third.getId());
        // La página posterior a la última devuelve vacío.
        assertThat(beyondLast.getContent()).isEmpty();
        // Una página vacía posterior a la última sigue informando del total de A.
        assertThat(beyondLast.getTotalElements()).isEqualTo(3L);
    }

    @Test
    void appliesOptionalOwnerAndActivityFiltersToContentAndTotal() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        em.persist(companyA);
        em.persist(companyB);
        User ownerA = persistUser(companyA);
        User anotherA = persistUser(companyA);
        User ownerB = persistUser(companyB);
        Device activeOwnerA = persistDevice(companyA, ownerA);
        Device inactiveOwnerA = persistDevice(companyA, ownerA);
        inactiveOwnerA.deactivate();
        Device activeAnotherA = persistDevice(companyA, anotherA);
        persistDevice(companyB, ownerB);
        em.flush();
        em.clear();

        Page<Device> ownerOnly = repository.findByCompanyWithFilters(
                companyA.getId(), ownerA.getId(), null, PageRequest.of(0, 1));
        Page<Device> activeOnly = repository.findByCompanyWithFilters(
                companyA.getId(), null, true, PageRequest.of(0, 1));
        Page<Device> inactiveOwner = repository.findByCompanyWithFilters(
                companyA.getId(), ownerA.getId(), false, PageRequest.of(0, 1));
        Page<Device> activeOwner = repository.findByCompanyWithFilters(
                companyA.getId(), ownerA.getId(), true, PageRequest.of(0, 1));
        Page<Device> foreignOwner = repository.findByCompanyWithFilters(
                companyA.getId(), ownerB.getId(), null, PageRequest.of(0, 1));
        Page<Device> secondActive = repository.findByCompanyWithFilters(
                companyA.getId(), null, true, PageRequest.of(1, 1));

        // El filtro de propietario devuelve su primera instalación.
        assertThat(ownerOnly.getContent()).extracting(Device::getId).containsExactly(activeOwnerA.getId());
        // El total del propietario incluye su Device activo y el inactivo, no el de otro usuario de A.
        assertThat(ownerOnly.getTotalElements()).isEqualTo(2L);
        // El total de activos excluye la instalación inactiva y la de otra compañía.
        assertThat(activeOnly.getTotalElements()).isEqualTo(2L);
        // La segunda página de activos encuentra al otro propietario de la misma Company.
        assertThat(secondActive.getContent()).extracting(Device::getId).containsExactly(activeAnotherA.getId());
        // Combinar propietario y false encuentra solo su instalación inactiva.
        assertThat(inactiveOwner.getContent()).extracting(Device::getId).containsExactly(inactiveOwnerA.getId());
        // El total conserva la combinación exacta de ambos filtros.
        assertThat(inactiveOwner.getTotalElements()).isEqualTo(1L);
        // Combinar propietario y true encuentra solo su instalación activa.
        assertThat(activeOwner.getContent()).extracting(Device::getId).containsExactly(activeOwnerA.getId());
        // Un propietario ajeno no amplía nunca el ámbito de Company.
        assertThat(foreignOwner.getContent()).isEmpty();
        // Una búsqueda sin coincidencias devuelve total cero.
        assertThat(foreignOwner.getTotalElements()).isZero();
        // Sin coincidencias tampoco existen páginas.
        assertThat(foreignOwner.getTotalPages()).isZero();
    }

    private User persistUser(Company company) {
        User user = new User("Ana", "Demo", UUID.randomUUID() + "@example.com", HASH, UserRole.USER, company);
        em.persist(user);
        return user;
    }

    private Device persistDevice(Company company, User owner) {
        License license = new License(company, 5);
        Policy policy = new Policy("Política de prueba", company);
        em.persist(license);
        em.persist(policy);
        Instant now = Instant.parse("2026-10-23T08:00:00Z");
        EnrollmentToken enrollment = new EnrollmentToken(license, owner, policy,
                UUID.randomUUID().toString().replace("-", "").repeat(2), now, now.plusSeconds(3600));
        enrollment.markUsed(now);
        em.persist(enrollment);
        Device device = new Device(UUID.randomUUID().toString(), "Equipo de prueba",
                company, owner, license, policy, enrollment);
        em.persist(device);
        return device;
    }
}
