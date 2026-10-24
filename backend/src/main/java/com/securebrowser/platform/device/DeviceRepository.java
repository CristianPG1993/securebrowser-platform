package com.securebrowser.platform.device;

import java.util.Optional;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.QueryHints;
import jakarta.persistence.LockModeType;
import jakarta.persistence.QueryHint;

/** Resuelve instalaciones dentro del ámbito autorizado del solicitante. */
public interface DeviceRepository extends JpaRepository<Device, Long> {

    /** Bloquea la instalación propia antes de consultar su política y registrar lastSeenAt. */
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @QueryHints(@QueryHint(name = "jakarta.persistence.lock.timeout", value = "0"))
    Optional<Device> findByIdAndCompany_IdAndUser_Id(Long id, Long companyId, Long userId);

    /** Resuelve filtros de historial sin bloquear ni simular una comunicación de instalación. */
    @Query("""
            select d from Device d where d.id = :id and d.company.id = :companyId
              and (:userId is null or d.user.id = :userId)
            """)
    Optional<Device> findInScope(@Param("id") Long id, @Param("companyId") Long companyId,
            @Param("userId") Long userId);

    /** La página y su total conservan Company y los filtros opcionales del caso de uso. */
    @Query(value = """
            select d from Device d
            where d.company.id = :companyId
              and (:userId is null or d.user.id = :userId)
              and (:active is null or d.active = :active)
            order by d.id asc
            """, countQuery = """
            select count(d) from Device d
            where d.company.id = :companyId
              and (:userId is null or d.user.id = :userId)
              and (:active is null or d.active = :active)
            """)
    Page<Device> findByCompanyWithFilters(@Param("companyId") Long companyId,
            @Param("userId") Long userId, @Param("active") Boolean active, Pageable pageable);
}
