package com.securebrowser.platform.securityevent;

import java.util.Optional;
import java.util.UUID;
import java.time.Instant;

import org.springframework.data.domain.Page;
import org.springframework.data.domain.Pageable;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

/** Persiste eventos y permite reconocer los UUID recibidos anteriormente. */
public interface SecurityEventRepository extends JpaRepository<SecurityEvent, Long> {

    /** Búsqueda interna global para detectar reenvíos y conflictos entre instalaciones. */
    Optional<SecurityEvent> findByEventUuid(UUID eventUuid);

    /** La unicidad global reconoce también un UUID insertado por otra transacción. */
    @Modifying
    @Query(value = """
            insert into security_events (event_uuid, device_id, type, details, occurred_at, received_at)
            values (:uuid, :deviceId, :type, :details, :occurredAt, :receivedAt)
            on conflict on constraint uq_security_events_event_uuid do nothing
            """, nativeQuery = true)
    int insertIfAbsent(@Param("uuid") UUID uuid, @Param("deviceId") Long deviceId,
            @Param("type") String type, @Param("details") String details,
            @Param("occurredAt") Instant occurredAt, @Param("receivedAt") Instant receivedAt);

    /** Contenido y total aplican exactamente el mismo ámbito y filtros temporales. */
    @Query(value = """
            select e from SecurityEvent e
            where e.device.company.id = :companyId
              and (:userId is null or e.device.user.id = :userId)
              and (:deviceId is null or e.device.id = :deviceId)
              and (:type is null or e.type = :type)
              and e.occurredAt >= coalesce(:from, e.occurredAt)
              and (cast(:to as Instant) is null or e.occurredAt < :to)
            order by e.occurredAt desc, e.id desc
            """, countQuery = """
            select count(e) from SecurityEvent e
            where e.device.company.id = :companyId
              and (:userId is null or e.device.user.id = :userId)
              and (:deviceId is null or e.device.id = :deviceId)
              and (:type is null or e.type = :type)
              and e.occurredAt >= coalesce(:from, e.occurredAt)
              and (cast(:to as Instant) is null or e.occurredAt < :to)
            """)
    Page<SecurityEvent> findInScope(@Param("companyId") Long companyId, @Param("userId") Long userId,
            @Param("deviceId") Long deviceId, @Param("type") SecurityEventType type,
            @Param("from") Instant from, @Param("to") Instant to, Pageable pageable);
}
