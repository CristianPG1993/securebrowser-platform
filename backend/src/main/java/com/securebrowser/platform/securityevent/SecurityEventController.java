package com.securebrowser.platform.securityevent;

import java.time.Instant;
import java.util.Set;
import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.securityevent.dto.SecurityEventBatchRequest;
import com.securebrowser.platform.securityevent.dto.SecurityEventBatchResponse;
import com.securebrowser.platform.securityevent.dto.SecurityEventRequest;
import com.securebrowser.platform.securityevent.dto.SecurityEventResponse;
import org.springframework.http.MediaType;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.util.MultiValueMap;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/** Publica recepción de lotes y consulta paginada, sin edición ni borrado. */
@RestController
@RequestMapping("/api/v1")
public class SecurityEventController {
    private final SecurityEventService service;
    public SecurityEventController(SecurityEventService service) { this.service = service; }

    @PostMapping(value = "/devices/{id}/events/batch", consumes = MediaType.APPLICATION_JSON_VALUE,
            produces = MediaType.APPLICATION_JSON_VALUE)
    public SecurityEventBatchResponse receive(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @PathVariable Long id, @RequestBody SecurityEventBatchRequest request,
            @RequestParam MultiValueMap<String, String> parameters) {
        if (!parameters.isEmpty()) throw new InvalidSecurityEventRequestException();
        return service.receive(actor, id, request);
    }

    @GetMapping(value = "/security-events", produces = MediaType.APPLICATION_JSON_VALUE)
    public PageResponse<SecurityEventResponse> list(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @RequestParam MultiValueMap<String, String> parameters) {
        if (!Set.of("page", "size", "deviceId", "type", "from", "to").containsAll(parameters.keySet())
                || parameters.values().stream().anyMatch(values -> values.size() != 1)) {
            throw new InvalidSecurityEventRequestException();
        }
        int page;
        int size;
        Long device;
        SecurityEventType type;
        Instant from;
        Instant to;
        try {
            page = parameters.containsKey("page") ? Integer.parseInt(parameters.getFirst("page")) : 0;
            size = parameters.containsKey("size") ? Integer.parseInt(parameters.getFirst("size")) : 20;
            device = parameters.containsKey("deviceId") ? Long.valueOf(parameters.getFirst("deviceId")) : null;
            type = parameters.containsKey("type")
                    ? SecurityEventType.valueOf(parameters.getFirst("type")) : null;
            from = parameters.containsKey("from") ? SecurityEventRequest.parseUtc(parameters.getFirst("from")) : null;
            to = parameters.containsKey("to") ? SecurityEventRequest.parseUtc(parameters.getFirst("to")) : null;
        } catch (IllegalArgumentException exception) {
            throw new InvalidSecurityEventRequestException();
        }
        return service.list(actor, page, size, device, type, from, to);
    }
}
