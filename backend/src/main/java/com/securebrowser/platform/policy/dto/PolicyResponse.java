package com.securebrowser.platform.policy.dto;

import java.time.Instant;
import java.util.List;
import com.securebrowser.platform.policy.FilterMode;
import com.securebrowser.platform.policy.Policy;

/** Snapshot público construido dentro de la transacción que lee la política y sus reglas. */
public record PolicyResponse(Long id, Long companyId, String name,
        boolean urlFilteringEnabled, FilterMode urlFilteringMode, List<String> urlRules,
        boolean downloadControlEnabled, FilterMode downloadControlMode, List<String> downloadRules,
        Instant createdAt, Instant updatedAt) {
    public PolicyResponse {
        urlRules = List.copyOf(urlRules);
        downloadRules = List.copyOf(downloadRules);
    }

    public static PolicyResponse from(Policy policy) {
        return new PolicyResponse(policy.getId(), policy.getCompany().getId(), policy.getName(),
                policy.isUrlFilteringEnabled(), policy.getUrlFilteringMode(),
                policy.getUrlRules().stream().map(rule -> rule.getDomain()).sorted().toList(),
                policy.isDownloadControlEnabled(), policy.getDownloadControlMode(),
                policy.getDownloadRules().stream().map(rule -> rule.getExtension()).sorted().toList(),
                policy.getCreatedAt(), policy.getUpdatedAt());
    }
}
