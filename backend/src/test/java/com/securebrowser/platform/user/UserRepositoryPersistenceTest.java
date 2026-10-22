package com.securebrowser.platform.user;

import static org.assertj.core.api.Assertions.assertThat;

import java.util.Optional;
import java.util.UUID;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las consultas de UserRepository sobre PostgreSQL real y temporal. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Import(PostgresRepositoryTestConfiguration.class)
@Transactional
class UserRepositoryPersistenceTest {

    // Hash BCrypt público que solo se utiliza para preparar usuarios de prueba.
    private static final String PASSWORD_HASH =
            "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    // Repositorio real generado por Spring Data JPA.
    @Autowired
    private UserRepository userRepository;

    // Prepara los datos y vacía la caché de entidades antes de consultar.
    @PersistenceContext
    private EntityManager entityManager;

    /** Un usuario solo se encuentra cuando coinciden su id y su compañía. */
    @Test
    void findsUserOnlyWithinRequestedCompany() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        entityManager.persist(companyA);
        entityManager.persist(companyB);

        User userA = persistAdmin(companyA);
        persistAdmin(companyB);
        entityManager.flush();
        entityManager.clear();

        Optional<User> ownCompanyResult = userRepository.findByIdAndCompany_Id(
                userA.getId(), companyA.getId());

        // Comprueba que el Optional contiene un usuario al buscarlo en su compañía.
        assertThat(ownCompanyResult).isPresent();

        // Comprueba que el usuario encontrado es exactamente el usuario A.
        assertThat(ownCompanyResult.orElseThrow().getId()).isEqualTo(userA.getId());

        // Comprueba que la compañía del usuario encontrado es la compañía A.
        assertThat(ownCompanyResult.orElseThrow().getCompany().getId())
                .isEqualTo(companyA.getId());

        // Comprueba que el usuario A no aparece al buscarlo dentro de la compañía B.
        assertThat(userRepository.findByIdAndCompany_Id(userA.getId(), companyB.getId()))
                .isEmpty();

        // Comprueba que un id de usuario inexistente devuelve un Optional vacío.
        assertThat(userRepository.findByIdAndCompany_Id(Long.MAX_VALUE, companyA.getId()))
                .isEmpty();
    }

    /** Comprueba el contenido, orden y totales de páginas limitadas a una compañía. */
    @Test
    void paginatesUsersOnlyWithinRequestedCompanyOrderedById() {
        Company companyA = new Company("Empresa A");
        Company companyB = new Company("Empresa B");
        entityManager.persist(companyA);
        entityManager.persist(companyB);

        // Intercala usuarios de ambas compañías para comprobar el filtro y el orden.
        User firstUserA = persistAdmin(companyA);
        persistAdmin(companyB);
        User secondUserA = persistAdmin(companyA);
        persistAdmin(companyB);
        User thirdUserA = persistAdmin(companyA);
        entityManager.flush();
        entityManager.clear();

        Page<User> firstPage = userRepository.findByCompany_IdOrderByIdAsc(
                companyA.getId(), PageRequest.of(0, 2));
        Page<User> secondPage = userRepository.findByCompany_IdOrderByIdAsc(
                companyA.getId(), PageRequest.of(1, 2));
        Page<User> beyondLastPage = userRepository.findByCompany_IdOrderByIdAsc(
                companyA.getId(), PageRequest.of(2, 2));

        // La primera página contiene los dos primeros usuarios de A, en orden de id.
        assertThat(firstPage.getContent()).extracting(user -> user.getId())
                .containsExactly(firstUserA.getId(), secondUserA.getId());

        // El total cuenta solo los tres usuarios de A, aunque B también tenga usuarios.
        assertThat(firstPage.getTotalElements()).isEqualTo(3L);

        // Tres usuarios repartidos de dos en dos necesitan dos páginas.
        assertThat(firstPage.getTotalPages()).isEqualTo(2);

        // La segunda página contiene únicamente el tercer usuario de A.
        assertThat(secondPage.getContent()).extracting(user -> user.getId())
                .containsExactly(thirdUserA.getId());

        // Cambiar de página conserva el total de usuarios de la compañía A.
        assertThat(secondPage.getTotalElements()).isEqualTo(3L);

        // Pedir una página posterior a la última devuelve una lista vacía.
        assertThat(beyondLastPage.getContent()).isEmpty();

        // Una página vacía posterior a la última conserva el total de usuarios de A.
        assertThat(beyondLastPage.getTotalElements()).isEqualTo(3L);
    }

    /** El filtro de rol limita tanto el contenido como el recuento de la compañía. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void filtersRoleWithinCompanyAndPaginates(UserRole role) {
        Company companyA = persistCompany("Empresa A");
        Company companyB = persistCompany("Empresa B");
        User first = persistUser(companyA, role);
        persistUser(companyB, role);
        User second = persistUser(companyA, role);
        persistUser(companyA, role == UserRole.ADMIN ? UserRole.USER : UserRole.ADMIN);
        entityManager.flush();
        entityManager.clear();

        Page<User> firstPage = userRepository.findByCompany_IdAndRoleOrderByIdAsc(
                companyA.getId(), role, PageRequest.of(0, 1));
        Page<User> secondPage = userRepository.findByCompany_IdAndRoleOrderByIdAsc(
                companyA.getId(), role, PageRequest.of(1, 1));

        // Solo aparece el primer usuario del rol solicitado en A.
        assertThat(firstPage.getContent()).extracting(user -> user.getId())
                .containsExactly(first.getId());
        // El total excluye al usuario de B y al de A que tiene otro rol.
        assertThat(firstPage.getTotalElements()).isEqualTo(2L);
        // Dos usuarios con una plaza por página producen dos páginas.
        assertThat(firstPage.getTotalPages()).isEqualTo(2);
        // La segunda página conserva el orden ascendente dentro del mismo filtro.
        assertThat(secondPage.getContent()).extracting(user -> user.getId())
                .containsExactly(second.getId());
        // El total del filtro no cambia al avanzar de página.
        assertThat(secondPage.getTotalElements()).isEqualTo(2L);
    }

    /** Una compañía sin usuarios no hereda contenido ni totales de otras compañías. */
    @Test
    void returnsEmptyPagesWhenCompanyHasNoUsers() {
        Company emptyCompany = persistCompany("Compañía sin usuarios");
        persistAdmin(persistCompany("Otra compañía"));
        entityManager.flush();
        entityManager.clear();

        Page<User> all = userRepository.findByCompany_IdOrderByIdAsc(
                emptyCompany.getId(), PageRequest.of(0, 20));
        Page<User> admins = userRepository.findByCompany_IdAndRoleOrderByIdAsc(
                emptyCompany.getId(), UserRole.ADMIN, PageRequest.of(0, 20));

        // La compañía vacía no contiene usuarios de la otra compañía.
        assertThat(all.getContent()).isEmpty();
        // Cero usuarios implican cero elementos totales.
        assertThat(all.getTotalElements()).isZero();
        // Cero elementos implican cero páginas según el contrato.
        assertThat(all.getTotalPages()).isZero();
        // El filtro de rol tampoco recupera usuarios ajenos.
        assertThat(admins.getContent()).isEmpty();
        // El recuento filtrado también permanece vacío.
        assertThat(admins.getTotalElements()).isZero();
        // La paginación filtrada mantiene cero páginas.
        assertThat(admins.getTotalPages()).isZero();
    }

    /** El email normalizado es único globalmente, sin filtro de compañía. */
    @Test
    void findsNormalizedEmailGlobally() {
        Company company = persistCompany("Empresa del titular");
        String address = UUID.randomUUID() + "@example.com";
        User user = new User("Ana", "García", "  " + address.toUpperCase(java.util.Locale.ROOT)
                + "  ", PASSWORD_HASH, UserRole.ADMIN, company);
        entityManager.persist(user);
        entityManager.flush();
        entityManager.clear();

        // La búsqueda global detecta el email que la entidad guardó normalizado.
        assertThat(userRepository.existsByEmail(address)).isTrue();
        // Un email distinto no está ocupado.
        assertThat(userRepository.existsByEmail(UUID.randomUUID() + "@example.com")).isFalse();
    }

    /** Al editar, mantener el email propio no se considera un duplicado. */
    @Test
    void excludesEditedUserFromEmailConflictCheck() {
        User owner = persistAdmin(persistCompany("Empresa A"));
        User other = persistAdmin(persistCompany("Empresa B"));
        entityManager.flush();
        entityManager.clear();

        // El usuario puede conservar su propio email.
        assertThat(userRepository.existsByEmailAndIdNot(owner.getEmail(), owner.getId())).isFalse();
        // Otro usuario, incluso de otra compañía, no puede apropiarse de ese email.
        assertThat(userRepository.existsByEmailAndIdNot(owner.getEmail(), other.getId())).isTrue();
        // Un email nuevo sigue disponible al excluir al usuario editado.
        assertThat(userRepository.existsByEmailAndIdNot(
                UUID.randomUUID() + "@example.com", owner.getId())).isFalse();
    }

    /** Los recuentos distinguen compañía y rol para el futuro control del último ADMIN. */
    @Test
    void countsEachRoleOnlyWithinCompany() {
        Company companyA = persistCompany("Empresa A");
        Company companyB = persistCompany("Empresa B");
        persistAdmin(companyA);
        persistAdmin(companyA);
        persistUser(companyA, UserRole.USER);
        persistAdmin(companyB);
        entityManager.flush();
        entityManager.clear();

        // A tiene dos ADMIN, sin sumar al ADMIN de B.
        assertThat(userRepository.countByCompany_IdAndRole(companyA.getId(), UserRole.ADMIN))
                .isEqualTo(2L);
        // El recuento de USER de A no incluye sus ADMIN.
        assertThat(userRepository.countByCompany_IdAndRole(companyA.getId(), UserRole.USER))
                .isEqualTo(1L);
        // B mantiene un único ADMIN independiente del recuento de A.
        assertThat(userRepository.countByCompany_IdAndRole(companyB.getId(), UserRole.ADMIN))
                .isEqualTo(1L);
        // Una compañía inexistente tiene cero usuarios del rol solicitado.
        assertThat(userRepository.countByCompany_IdAndRole(Long.MAX_VALUE, UserRole.ADMIN)).isZero();
    }

    /** La consulta bloqueante conserva el mismo ámbito de compañía que la consulta normal. */
    @Test
    void lockedLookupStillChecksCompanyAndMissingUser() {
        Company companyA = persistCompany("Empresa A");
        Company companyB = persistCompany("Empresa B");
        User user = persistAdmin(companyA);
        persistAdmin(companyB);
        entityManager.flush();
        entityManager.clear();

        // El usuario ajeno no aparece aunque la consulta solicite un bloqueo.
        assertThat(userRepository.findByIdAndCompanyIdForUpdate(user.getId(), companyB.getId())).isEmpty();
        // Un id inexistente tampoco se convierte en un usuario bloqueado.
        assertThat(userRepository.findByIdAndCompanyIdForUpdate(Long.MAX_VALUE, companyA.getId())).isEmpty();
        // La consulta puede bloquear y devolver al usuario de la compañía correcta.
        assertThat(userRepository.findByIdAndCompanyIdForUpdate(user.getId(), companyA.getId()))
                .isPresent();
    }

    /** Mantiene un ADMIN en cada compañía y evita colisiones de email entre pruebas. */
    private User persistAdmin(Company company) {
        return persistUser(company, UserRole.ADMIN);
    }

    /** Permite preparar usuarios de cualquiera de los dos roles del dominio. */
    private User persistUser(Company company, UserRole role) {
        User user = new User(
                "Ana", "García", UUID.randomUUID() + "@example.com",
                PASSWORD_HASH, role, company);
        entityManager.persist(user);
        return user;
    }

    /** Crea una compañía para preparar los datos que necesita cada consulta. */
    private Company persistCompany(String name) {
        Company company = new Company(name);
        entityManager.persist(company);
        return company;
    }
}
