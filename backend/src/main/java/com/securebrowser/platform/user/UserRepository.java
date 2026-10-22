package com.securebrowser.platform.user;

import java.util.Optional;

import jakarta.persistence.LockModeType;
import jakarta.persistence.QueryHint;

import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.jpa.repository.QueryHints;
import org.springframework.data.repository.query.Param;

/** Proporciona acceso a la persistencia de User mediante Spring Data JPA. */
public interface UserRepository extends JpaRepository<User, Long> {

    /** Busca un usuario solo dentro de la compañía indicada. */
    Optional<User> findByIdAndCompany_Id(Long id, Long companyId);

    /** Lista los usuarios de una compañía por id ascendente y con paginación. */
    Page<User> findByCompany_IdOrderByIdAsc(Long companyId, Pageable pageable);

    /** Lista los usuarios de una compañía y un rol por id ascendente y con paginación. */
    Page<User> findByCompany_IdAndRoleOrderByIdAsc(
            Long companyId, UserRole role, Pageable pageable);

    /** Comprueba la existencia global de un email ya normalizado por el dominio. */
    boolean existsByEmail(String email);

    /** Comprueba duplicados al editar, excluyendo al usuario que conserva su email. */
    boolean existsByEmailAndIdNot(String email, Long id);

    /** Cuenta usuarios de un rol dentro de una compañía, por ejemplo sus ADMIN. */
    long countByCompany_IdAndRole(Long companyId, UserRole role);

    /**
     * Bloquea el usuario de la compañía dentro de la transacción del servicio.
     * Si también se necesita Company, debe bloquearse primero; NOWAIT evita esperas de fila.
     */
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @QueryHints(@QueryHint(name = "jakarta.persistence.lock.timeout", value = "0"))
    @Query("select u from User u where u.id = :id and u.company.id = :companyId")
    Optional<User> findByIdAndCompanyIdForUpdate(
            @Param("id") Long id, @Param("companyId") Long companyId);
}
