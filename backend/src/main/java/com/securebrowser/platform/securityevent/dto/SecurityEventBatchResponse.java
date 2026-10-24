package com.securebrowser.platform.securityevent.dto;

import java.util.List;
import java.util.UUID;

/** Confirma UUID únicos, conservando el orden de primera aparición en la petición. */
public record SecurityEventBatchResponse(List<UUID> acceptedEventUuids) {
    public SecurityEventBatchResponse { acceptedEventUuids = List.copyOf(acceptedEventUuids); }
}
