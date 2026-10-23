package com.securebrowser.platform.policy;

import java.util.Set;
import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.policy.dto.PolicyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;
import org.springframework.http.MediaType;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.util.MultiValueMap;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/** Publica exclusivamente las consultas y la edición administrativa de Policy. */
@RestController
@RequestMapping("/api/v1/policies")
public class PolicyController {
    private final PolicyService service;

    public PolicyController(PolicyService service) {
        this.service = service;
    }

    @GetMapping(produces = MediaType.APPLICATION_JSON_VALUE)
    public PageResponse<PolicyResponse> list(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @RequestParam MultiValueMap<String, String> parameters) {
        if (!Set.of("page", "size").containsAll(parameters.keySet())
                || parameters.values().stream().anyMatch(values -> values.size() != 1)) {
            throw new InvalidPolicyRequestException();
        }
        return service.list(actor, integer(parameters, "page", 0), integer(parameters, "size", 20));
    }

    @GetMapping(value = "/{id}", produces = MediaType.APPLICATION_JSON_VALUE)
    public PolicyResponse get(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @PathVariable Long id, @RequestParam MultiValueMap<String, String> parameters) {
        requireNoParameters(parameters);
        return service.get(actor, id);
    }

    @PatchMapping(value = "/{id}", consumes = MediaType.APPLICATION_JSON_VALUE,
            produces = MediaType.APPLICATION_JSON_VALUE)
    public PolicyResponse update(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser actor,
            @PathVariable Long id, @RequestBody PolicyUpdateRequest request,
            @RequestParam MultiValueMap<String, String> parameters) {
        requireNoParameters(parameters);
        return service.update(actor, id, request);
    }

    private static int integer(MultiValueMap<String, String> parameters, String name, int fallback) {
        if (!parameters.containsKey(name)) return fallback;
        try {
            return Integer.parseInt(parameters.getFirst(name));
        } catch (NumberFormatException exception) {
            throw new InvalidPolicyRequestException();
        }
    }

    private static void requireNoParameters(MultiValueMap<String, String> parameters) {
        if (!parameters.isEmpty()) throw new InvalidPolicyRequestException();
    }
}
