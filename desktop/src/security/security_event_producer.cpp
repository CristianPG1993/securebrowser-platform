#include "security/security_event_producer.h"

#include <unicode/uchar.h>
#include <unicode/utf8.h>

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
    return ProduceBlocked(SecurityEventType::UrlBlocked, origin, occurredAt, UrlBlockReasonCode(reason),
                          reason == SecurityReason::InvalidUrl ? std::string_view{} : host, policyId);
}

bool SecurityEventProducer::ProduceDownloadBlocked(const SecurityContext& origin,
                                                  std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                                                  SecurityReason reason, std::string_view extension,
                                                  std::optional<std::int64_t> policyId) const noexcept {
    const bool hasExtension = reason == SecurityReason::ExtensionDenied || reason == SecurityReason::ExtensionNotAllowed;
    return ProduceBlocked(SecurityEventType::DownloadBlocked, origin, occurredAt, DownloadBlockReasonCode(reason),
                          hasExtension ? extension : std::string_view{}, policyId);
}

bool SecurityEventProducer::ProduceBlocked(SecurityEventType type, const SecurityContext& origin,
                                          std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                                          std::string_view reasonCode, std::string_view value,
                                          std::optional<std::int64_t> policyId) const noexcept {
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
        // El prefijo es ASCII. Se reservan las comillas y la llave final dentro del límite REST.
        constexpr std::size_t maxDetailsLength = 2000;
        const bool isUrl = type == SecurityEventType::UrlBlocked;
        const std::string_view field = isUrl ? R"(,"host":")" : R"(,"extension":")";
        const auto maxLength = maxDetailsLength - details.size() - field.size() - 2;
        const bool includeValue = isUrl ? value.size() <= maxLength && CanIncludeHost(value)
                                        : CanIncludeExtension(value, maxLength);
        if (includeValue) {
            details += field;
            details += value;
            details += '"';
        }
        details += '}';

        auto uuid = generateUuid_();
        if (!IsValidEventUuid(uuid)) {
            return false;
        }
        const SecurityEvent event{std::move(uuid), type, occurredAt, std::move(details)};
        return sink_(origin, event);
    } catch (...) {
        // Un fallo al producir o entregar el evento se comunica al llamador. El bloqueo ya se ha aplicado.
        return false;
    }
}

std::string_view SecurityEventProducer::ContextReasonCode(SecurityReason reason) noexcept {
    switch (reason) {
    case SecurityReason::InvalidContext: return "INVALID_CONTEXT";
    case SecurityReason::InstallationUnauthorized: return "INSTALLATION_UNAUTHORIZED";
    case SecurityReason::PolicyUnavailable: return "POLICY_UNAVAILABLE";
    case SecurityReason::PolicyExpired: return "POLICY_EXPIRED";
    default: return {};
    }
}

std::string_view SecurityEventProducer::UrlBlockReasonCode(SecurityReason reason) noexcept {
    switch (reason) {
    case SecurityReason::InvalidUrl: return "INVALID_URL";
    case SecurityReason::UnsupportedScheme: return "UNSUPPORTED_SCHEME";
    case SecurityReason::IpBlocked: return "IP_BLOCKED";
    case SecurityReason::DomainDenied: return "DOMAIN_DENIED";
    case SecurityReason::DomainNotAllowed: return "DOMAIN_NOT_ALLOWED";
    default: return ContextReasonCode(reason);
    }
}

std::string_view SecurityEventProducer::DownloadBlockReasonCode(SecurityReason reason) noexcept {
    switch (reason) {
    case SecurityReason::InvalidFileName: return "INVALID_FILE_NAME";
    case SecurityReason::MissingExtension: return "MISSING_EXTENSION";
    case SecurityReason::ExtensionDenied: return "EXTENSION_DENIED";
    case SecurityReason::ExtensionNotAllowed: return "EXTENSION_NOT_ALLOWED";
    default: return ContextReasonCode(reason);
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

bool SecurityEventProducer::CanIncludeExtension(std::string_view extension, std::size_t maxLength) noexcept {
    // Una extensión UTF-8 ocupa como máximo cuatro bytes por punto de código.
    if (extension.empty() || extension.size() > 4 * maxLength || extension.back() == ' '
        || extension.find_first_of(".<>:\"/\\|?*") != std::string_view::npos) {
        return false;
    }
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(extension.data());
    const auto size = static_cast<std::int32_t>(extension.size());
    std::int32_t offset = 0;
    std::size_t length = 0;
    while (offset < size) {
        UChar32 codePoint;
        U8_NEXT(bytes, offset, size, codePoint);
        if (codePoint < 0 || u_isISOControl(codePoint) || ++length > maxLength) {
            return false;
        }
    }
    return true;
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
