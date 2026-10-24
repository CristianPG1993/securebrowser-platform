package com.securebrowser.platform.securityevent.dto;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.UUID;

import com.fasterxml.jackson.annotation.JsonCreator;
import com.securebrowser.platform.securityevent.InvalidSecurityEventRequestException;
import com.securebrowser.platform.securityevent.SecurityEventType;
import tools.jackson.databind.JsonNode;

/** Lee exclusivamente los campos y tipos JSON del contrato de sincronización. */
public record SecurityEventBatchRequest(List<SecurityEventRequest> events) {
    public SecurityEventBatchRequest {
        if (events == null || events.isEmpty() || events.size() > 100 || events.stream().anyMatch(java.util.Objects::isNull)) {
            throw new InvalidSecurityEventRequestException();
        }
        events = List.copyOf(events);
    }

    @JsonCreator(mode = JsonCreator.Mode.DELEGATING)
    public static SecurityEventBatchRequest fromJson(JsonNode body) {
        requireFields(body, Set.of("events"));
        JsonNode array = body.get("events");
        if (array == null || !array.isArray() || array.isEmpty() || array.size() > 100) {
            throw new InvalidSecurityEventRequestException();
        }
        List<SecurityEventRequest> events = new ArrayList<>();
        for (JsonNode item : array) {
            requireFields(item, Set.of("eventUuid", "type", "occurredAt", "details"));
            String uuid = text(item.get("eventUuid"));
            if (!uuid.matches("[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}")) {
                throw new InvalidSecurityEventRequestException();
            }
            SecurityEventType type;
            try { type = SecurityEventType.valueOf(text(item.get("type"))); }
            catch (IllegalArgumentException exception) { throw new InvalidSecurityEventRequestException(); }
            JsonNode details = item.get("details");
            events.add(new SecurityEventRequest(UUID.fromString(uuid), type,
                    SecurityEventRequest.parseUtc(text(item.get("occurredAt"))),
                    details == null || details.isNull() ? null : text(details)));
        }
        return new SecurityEventBatchRequest(events);
    }

    private static void requireFields(JsonNode node, Set<String> allowed) {
        if (node == null || !node.isObject() || !allowed.containsAll(node.propertyNames())) {
            throw new InvalidSecurityEventRequestException();
        }
    }

    private static String text(JsonNode node) {
        if (node == null || !node.isTextual()) throw new InvalidSecurityEventRequestException();
        return node.stringValue();
    }
}
