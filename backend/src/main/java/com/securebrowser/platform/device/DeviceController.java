package com.securebrowser.platform.device;

import java.util.Set;
import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.device.dto.DeviceResponse;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.security.AuthenticatedUser;
import org.springframework.http.MediaType;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.util.MultiValueMap;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/** Publica únicamente listado y política de instalación del backend mínimo. */
@RestController
@RequestMapping("/api/v1/devices")
public class DeviceController {
    private final DeviceService service;

    public DeviceController(DeviceService service) { this.service = service; }

    @GetMapping(produces = MediaType.APPLICATION_JSON_VALUE)
    public PageResponse<DeviceResponse> list(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @RequestParam MultiValueMap<String, String> parameters) {
        if (!Set.of("page", "size", "userId", "active").containsAll(parameters.keySet())
                || parameters.values().stream().anyMatch(values -> values.size() != 1)) {
            throw new InvalidDeviceRequestException();
        }
        try {
            int page = parameters.containsKey("page") ? Integer.parseInt(parameters.getFirst("page")) : 0;
            int size = parameters.containsKey("size") ? Integer.parseInt(parameters.getFirst("size")) : 20;
            Long userId = parameters.containsKey("userId") ? Long.valueOf(parameters.getFirst("userId")) : null;
            Boolean active = null;
            if (parameters.containsKey("active")) {
                String value = parameters.getFirst("active");
                if (!"true".equals(value) && !"false".equals(value)) throw new InvalidDeviceRequestException();
                active = Boolean.valueOf(value);
            }
            return service.list(actor, page, size, userId, active);
        } catch (NumberFormatException exception) {
            throw new InvalidDeviceRequestException();
        }
    }

    @GetMapping(value = "/{id}/policy", produces = MediaType.APPLICATION_JSON_VALUE)
    public PolicyResponse getPolicy(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @PathVariable Long id, @RequestParam MultiValueMap<String, String> parameters) {
        if (!parameters.isEmpty()) throw new InvalidDeviceRequestException();
        return service.getPolicy(actor, id);
    }
}
