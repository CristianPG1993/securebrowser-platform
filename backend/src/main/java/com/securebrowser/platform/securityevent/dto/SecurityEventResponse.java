package com.securebrowser.platform.securityevent.dto;

import java.time.Instant;
import java.util.UUID;
import com.securebrowser.platform.securityevent.SecurityEvent;
import com.securebrowser.platform.securityevent.SecurityEventType;

/** Snapshot del evento, sin exponer la entidad ni información de otros recursos. */
public record SecurityEventResponse(Long id, UUID eventUuid, Long deviceId, SecurityEventType type,
        String details, Instant occurredAt, Instant receivedAt) {
    public static SecurityEventResponse from(SecurityEvent event) {
        return new SecurityEventResponse(event.getId(), event.getEventUuid(), event.getDevice().getId(),
                event.getType(), event.getDetails(), event.getOccurredAt(), event.getReceivedAt());
    }
}
