package com.securebrowser.platform.policy;

import java.util.Optional;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.jpa.repository.QueryHints;
import org.springframework.data.repository.query.Param;
import jakarta.persistence.LockModeType;
import jakarta.persistence.QueryHint;

/** Proporciona acceso a las políticas limitado al ámbito de una compañía. */
public interface PolicyRepository extends JpaRepository<Policy, Long> {

    /** Busca una política únicamente cuando coinciden su id y su compañía. */
    Optional<Policy> findByIdAndCompany_Id(Long id, Long companyId);

    /** Aplica el mismo ámbito al contenido paginado y a su total. */
    Page<Policy> findByCompany_IdOrderByIdAsc(Long companyId, Pageable pageable);

    /** Serializa la edición de configuración y reglas sin esperar indefinidamente. */
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @QueryHints(@QueryHint(name = "jakarta.persistence.lock.timeout", value = "0"))
    @Query("select p from Policy p where p.id = :id and p.company.id = :companyId")
    Optional<Policy> findByIdAndCompanyIdForUpdate(@Param("id") Long id, @Param("companyId") Long companyId);
}
