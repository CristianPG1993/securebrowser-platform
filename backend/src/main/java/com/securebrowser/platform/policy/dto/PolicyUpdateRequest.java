package com.securebrowser.platform.policy.dto;

import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import com.fasterxml.jackson.annotation.JsonCreator;
import com.securebrowser.platform.policy.FilterMode;
import tools.jackson.databind.JsonNode;

/**
 * DTO de PATCH: null interno significa omisión; un null recibido en JSON se rechaza.
 * Lee los tipos originales para impedir conversiones implícitas de booleanos, enums y listas.
 */
public final class PolicyUpdateRequest {
    private static final Set<String> FIELDS = Set.of("name", "urlFilteringEnabled", "urlFilteringMode",
            "urlRules", "downloadControlEnabled", "downloadControlMode", "downloadRules");

    private final String name;
    private final Boolean urlFilteringEnabled;
    private final FilterMode urlFilteringMode;
    private final List<String> urlRules;
    private final Boolean downloadControlEnabled;
    private final FilterMode downloadControlMode;
    private final List<String> downloadRules;

    private PolicyUpdateRequest(JsonNode node) {
        name = text(node, "name");
        urlFilteringEnabled = flag(node, "urlFilteringEnabled");
        urlFilteringMode = mode(node, "urlFilteringMode");
        urlRules = rules(node, "urlRules");
        downloadControlEnabled = flag(node, "downloadControlEnabled");
        downloadControlMode = mode(node, "downloadControlMode");
        downloadRules = rules(node, "downloadRules");
    }

    @JsonCreator(mode = JsonCreator.Mode.DELEGATING)
    public static PolicyUpdateRequest fromJson(JsonNode node) {
        if (node == null || !node.isObject() || node.isEmpty()) {
            throw invalid();
        }
        for (String field : node.propertyNames()) {
            if (!FIELDS.contains(field) || node.get(field).isNull()) {
                throw invalid();
            }
        }
        return new PolicyUpdateRequest(node);
    }

    private static String text(JsonNode node, String field) {
        JsonNode value = node.get(field);
        if (value == null) return null;
        if (!value.isTextual()) throw invalid();
        return value.stringValue();
    }

    private static Boolean flag(JsonNode node, String field) {
        JsonNode value = node.get(field);
        if (value == null) return null;
        if (!value.isBoolean()) throw invalid();
        return value.booleanValue();
    }

    private static FilterMode mode(JsonNode node, String field) {
        String value = text(node, field);
        return value == null ? null : FilterMode.valueOf(value);
    }

    private static List<String> rules(JsonNode node, String field) {
        JsonNode value = node.get(field);
        if (value == null) return null;
        if (!value.isArray()) throw invalid();
        List<String> result = new ArrayList<>();
        for (JsonNode entry : value) {
            if (!entry.isTextual()) throw invalid();
            result.add(entry.stringValue());
        }
        return List.copyOf(result);
    }

    private static IllegalArgumentException invalid() {
        return new IllegalArgumentException("Invalid policy patch structure");
    }

    public String name() { return name; }
    public Boolean urlFilteringEnabled() { return urlFilteringEnabled; }
    public FilterMode urlFilteringMode() { return urlFilteringMode; }
    public List<String> urlRules() { return urlRules; }
    public Boolean downloadControlEnabled() { return downloadControlEnabled; }
    public FilterMode downloadControlMode() { return downloadControlMode; }
    public List<String> downloadRules() { return downloadRules; }
}
