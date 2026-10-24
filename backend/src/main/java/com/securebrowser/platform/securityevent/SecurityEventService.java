package com.securebrowser.platform.securityevent;

import java.time.Clock;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Objects;
import java.util.UUID;

import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.device.Device;
import com.securebrowser.platform.device.DeviceNotFoundException;
import com.securebrowser.platform.device.DeviceRepository;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.securityevent.dto.SecurityEventBatchRequest;
import com.securebrowser.platform.securityevent.dto.SecurityEventBatchResponse;
import com.securebrowser.platform.securityevent.dto.SecurityEventRequest;
import com.securebrowser.platform.securityevent.dto.SecurityEventResponse;
import com.securebrowser.platform.user.UserRole;
import org.springframework.data.domain.PageRequest;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Isolation;
import org.springframework.transaction.annotation.Transactional;

/** Recibe lotes atómicos y consulta el historial dentro del ámbito autenticado. */
@Service
public class SecurityEventService {
    private final SecurityEventRepository events;
    private final DeviceRepository devices;
    private final Clock clock;

    public SecurityEventService(SecurityEventRepository events, DeviceRepository devices, Clock clock) {
        this.events = events;
        this.devices = devices;
        this.clock = clock;
    }

    /** READ_COMMITTED permite leer al ganador de una inserción concurrente tras ON CONFLICT. */
    @Transactional(isolation = Isolation.READ_COMMITTED, timeout = 10)
    public SecurityEventBatchResponse receive(AuthenticatedUser actor, Long deviceId, SecurityEventBatchRequest request) {
        requireIdentity(actor);
        requireId(deviceId);
        if (request == null) throw new InvalidSecurityEventRequestException();
        // Propiedad también para ADMIN; la actividad no impide enviar historial pendiente.
        Device device = devices.findByIdAndCompany_IdAndUser_Id(deviceId, actor.companyId(), actor.userId())
                .orElseThrow(DeviceNotFoundException::new);
        LinkedHashMap<UUID, SecurityEventRequest> unique = new LinkedHashMap<>();
        for (SecurityEventRequest event : request.events()) {
            SecurityEventRequest previous = unique.putIfAbsent(event.eventUuid(), event);
            if (previous != null && !previous.equals(event)) throw new EventUuidConflictException();
        }
        Instant now = clock.instant().truncatedTo(ChronoUnit.MICROS);
        // Orden estable de inserción para que lotes con varios UUID compartidos no se bloqueen en orden inverso.
        for (SecurityEventRequest event : unique.values().stream()
                .sorted(Comparator.comparing(value -> value.eventUuid().toString())).toList()) {
            int inserted = events.insertIfAbsent(event.eventUuid(), deviceId, event.type().name(),
                    event.details(), event.occurredAt(), now);
            if (inserted == 0) {
                SecurityEvent stored = events.findByEventUuid(event.eventUuid()).orElseThrow();
                if (!stored.getDevice().getId().equals(deviceId) || stored.getType() != event.type()
                        || !Objects.equals(stored.getDetails(), event.details())
                        || !stored.getOccurredAt().equals(event.occurredAt())) {
                    throw new EventUuidConflictException();
                }
            }
        }
        device.recordLastSeen(now);
        devices.flush();
        return new SecurityEventBatchResponse(List.copyOf(unique.keySet()));
    }

    @Transactional(readOnly = true, isolation = Isolation.REPEATABLE_READ)
    public PageResponse<SecurityEventResponse> list(AuthenticatedUser actor, int page, int size,
            Long deviceId, SecurityEventType type, Instant from, Instant to) {
        requireIdentity(actor);
        if (page < 0 || size < 1 || size > 100 || (from != null && to != null && !from.isBefore(to))) {
            throw new InvalidSecurityEventRequestException();
        }
        Long owner = actor.role() == UserRole.USER ? actor.userId() : null;
        if (deviceId != null) {
            requireId(deviceId);
            devices.findInScope(deviceId, actor.companyId(), owner).orElseThrow(DeviceNotFoundException::new);
        }
        return PageResponse.from(events.findInScope(actor.companyId(), owner, deviceId, type, from, to,
                PageRequest.of(page, size)).map(SecurityEventResponse::from));
    }

    private static void requireIdentity(AuthenticatedUser actor) {
        if (actor == null) throw new AccessDeniedException("Authenticated identity is required");
    }

    private static void requireId(Long id) {
        if (id == null || id <= 0) throw new InvalidSecurityEventRequestException();
    }
}
