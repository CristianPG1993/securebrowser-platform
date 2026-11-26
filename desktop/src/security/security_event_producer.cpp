#include "security/security_event_producer.h"

#include <stdexcept>
#include <utility>

namespace sbp {

SecurityEventProducer::SecurityEventProducer(UuidGenerator generateUuid, SecurityEventSink sink)
    : generateUuid_(std::move(generateUuid)), sink_(std::move(sink)) {
    // Ambas funciones deben estar disponibles antes de que el productor reciba su primer bloqueo.
    if (!generateUuid_ || !sink_) {
        throw std::invalid_argument("SecurityEventProducer requires a UUID generator and an event sink.");
    }
}

bool SecurityEventProducer::ProduceUrlBlocked(const SecurityContext& origin,
                                             std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                                             SecurityReason reason, std::string_view host,
                                             std::optional<std::int64_t> policyId) const noexcept {
    const auto reasonCode = UrlBlockReasonCode(reason);
    if (reasonCode.empty() || origin.Generation() == 0 || origin.DeviceId() <= 0 || origin.CompanyId() <= 0) {
        return false;
    }

    try {
        std::string details = R"({"schemaVersion":1,"reason":")";
        details += reasonCode;
        details += '"';
        if (policyId && *policyId > 0) {
            details += R"(,"policyId":)";
            details += std::to_string(*policyId);
        }
        // El adaptador entrega un host ASCII. Se conserva el motivo aunque el host falte o exceda el límite REST.
        constexpr std::size_t maxDetailsLength = 2000;
        constexpr std::string_view hostField = R"(,"host":")";
        // Dos caracteres cierran la cadena del host y el objeto JSON.
        const auto maxHostLength = maxDetailsLength - details.size() - hostField.size() - 2;
        if (reason != SecurityReason::InvalidUrl && host.size() <= maxHostLength && CanIncludeHost(host)) {
            details += hostField;
            details += host;
            details += '"';
        }
        details += '}';

        auto uuid = generateUuid_();
        if (!IsValidEventUuid(uuid)) {
            return false;
        }
        const SecurityEvent event{std::move(uuid), SecurityEventType::UrlBlocked, occurredAt, std::move(details)};
        return sink_(origin, event);
    } catch (...) {
        // Un fallo al producir o entregar el evento se comunica al llamador. El bloqueo ya se ha aplicado.
        return false;
    }
}

std::string_view SecurityEventProducer::UrlBlockReasonCode(SecurityReason reason) noexcept {
    switch (reason) {
    case SecurityReason::InvalidContext: return "INVALID_CONTEXT";
    case SecurityReason::InstallationUnauthorized: return "INSTALLATION_UNAUTHORIZED";
    case SecurityReason::PolicyUnavailable: return "POLICY_UNAVAILABLE";
    case SecurityReason::PolicyExpired: return "POLICY_EXPIRED";
    case SecurityReason::InvalidUrl: return "INVALID_URL";
    case SecurityReason::UnsupportedScheme: return "UNSUPPORTED_SCHEME";
    case SecurityReason::IpBlocked: return "IP_BLOCKED";
    case SecurityReason::DomainDenied: return "DOMAIN_DENIED";
    case SecurityReason::DomainNotAllowed: return "DOMAIN_NOT_ALLOWED";
    default: return {};
    }
}

bool SecurityEventProducer::CanIncludeHost(std::string_view host) noexcept {
    // Solo se copia un dominio ASCII o el literal IPv6 entre corchetes que entrega CEF.
    if (host.starts_with('[') && host.ends_with(']') && host.size() > 2) {
        const auto address = host.substr(1, host.size() - 2);
        return address.find(':') != std::string_view::npos
            && address.find_first_not_of("0123456789abcdefABCDEF:.") == std::string_view::npos;
    }
    return !host.empty()
        && host.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._") == std::string_view::npos;
}

bool SecurityEventProducer::IsValidEventUuid(std::string_view uuid) noexcept {
    if (uuid.size() != 36) {
        return false;
    }
    // El backend acepta el formato hexadecimal 8-4-4-4-12.
    constexpr std::string_view hexDigits = "0123456789abcdefABCDEF";
    for (std::size_t i = 0; i < uuid.size(); ++i) {
        const bool separator = i == 8 || i == 13 || i == 18 || i == 23;
        if (separator ? uuid[i] != '-' : hexDigits.find(uuid[i]) == std::string_view::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace sbp
