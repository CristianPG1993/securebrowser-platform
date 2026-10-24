package com.securebrowser.platform.securityevent.dto;

import java.time.Instant;
import java.time.OffsetDateTime;
import java.time.ZoneOffset;
import java.time.temporal.ChronoUnit;
import java.util.UUID;

import com.securebrowser.platform.securityevent.InvalidSecurityEventRequestException;
import com.securebrowser.platform.securityevent.SecurityEventType;

/** Conserva el contenido original y la precisión temporal del dominio persistido. */
public record SecurityEventRequest(UUID eventUuid, SecurityEventType type, Instant occurredAt, String details) {
    public SecurityEventRequest {
        if (eventUuid == null || type == null || occurredAt == null
                || (details != null && details.codePointCount(0, details.length()) > 2000)) {
            throw new InvalidSecurityEventRequestException();
        }
        occurredAt = occurredAt.truncatedTo(ChronoUnit.MICROS);
    }

    /** Exige zona UTC explícita; no interpreta fechas locales como UTC implícito. */
    public static Instant parseUtc(String value) {
        try {
            OffsetDateTime date = OffsetDateTime.parse(value);
            if (!date.getOffset().equals(ZoneOffset.UTC)) throw new InvalidSecurityEventRequestException();
            return date.toInstant();
        } catch (java.time.DateTimeException | NullPointerException exception) {
            throw new InvalidSecurityEventRequestException();
        }
    }
}
