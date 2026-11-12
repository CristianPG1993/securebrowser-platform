#include "security/url_filter.h"

namespace sbp {

// Coordina las comprobaciones y conserva el primer motivo que impide continuar.
SecurityDecision UrlFilter::Evaluate(
    const PolicyManager::PolicyReadResult& policy, const UrlTarget& target) const {
    SecurityReason reason = CheckPolicyAvailability(policy);
    if (reason != SecurityReason::Allowed) {
        return SecurityDecision(reason);
    }

    reason = CheckTarget(target);
    if (reason != SecurityReason::Allowed) {
        return SecurityDecision(reason);
    }

    // La comprobación inicial garantiza que el estado y la política están presentes.
    return SecurityDecision(EvaluateRules(policy.state->policy->UrlFiltering(), target));
}

// Rechaza lecturas sin contexto o política completa antes de evaluar el destino.
SecurityReason UrlFilter::CheckPolicyAvailability(
    const PolicyManager::PolicyReadResult& policy) {
    if (policy.availability == PolicyAvailability::InvalidContext || !policy.state) {
        return SecurityReason::InvalidContext;
    }

    if (!policy.state->policy || !policy.state->validatedAt.has_value()) {
        return SecurityReason::PolicyUnavailable;
    }

    if (policy.availability == PolicyAvailability::Expired) {
        return SecurityReason::PolicyExpired;
    }

    // Solo Available supera el control; Unavailable y valores desconocidos se rechazan.
    if (policy.availability != PolicyAvailability::Available) {
        return SecurityReason::PolicyUnavailable;
    }

    return SecurityReason::Allowed;
}

// Conserva la separación con el parser: comprueba sus datos sin reinterpretar la URL.
SecurityReason UrlFilter::CheckTarget(const UrlTarget& target) {
    if (!target.IsValid()) {
        return SecurityReason::InvalidUrl;
    }

    if (target.Scheme() != "http" && target.Scheme() != "https") {
        return SecurityReason::UnsupportedScheme;
    }

    if (target.Host().empty()) {
        return SecurityReason::InvalidUrl;
    }

    // La identificación real de dominio o IP corresponde al adaptador CEF.
    // Unknown y cualquier valor no reconocido se rechazan antes de aplicar las reglas.
    switch (target.HostKind()) {
    case UrlHostKind::Domain:
    case UrlHostKind::Ipv4:
    case UrlHostKind::Ipv6:
        return SecurityReason::Allowed;
    default:
        return SecurityReason::InvalidUrl;
    }
}

// Solo se utilizará con hosts clasificados como Domain, nunca con literales IP.
bool UrlFilter::MatchesDomain(const std::string& host, const std::string& rule) {
    std::string normalized = rule;
    for (char& character : normalized) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    if (!normalized.empty() && normalized.back() == '.') {
        normalized.pop_back();
    }
    if (normalized.empty()) {
        return false;
    }

    if (host == normalized) {
        return true;
    }

    // Exige un punto inmediatamente anterior al sufijo y comprueba el tamaño antes de indexar.
    return host.size() > normalized.size()
        && host.ends_with(normalized)
        && host[host.size() - normalized.size() - 1] == '.';
}

// Evalúa únicamente las reglas; los controles de política y destino se realizan antes.
SecurityReason UrlFilter::EvaluateRules(
    const UrlFilteringConfig& config, const UrlTarget& target) {
    // Un modo desconocido nunca se interpreta como una configuración permisiva.
    if (config.Mode() != FilterMode::Denylist && config.Mode() != FilterMode::Allowlist) {
        return SecurityReason::PolicyUnavailable;
    }

    if (!config.Enabled()) {
        return SecurityReason::Allowed;
    }

    if (target.HostKind() == UrlHostKind::Ipv4 || target.HostKind() == UrlHostKind::Ipv6) {
        return SecurityReason::IpBlocked;
    }

    bool matched = false;
    for (const std::string& rule : config.Rules()) {
        if (MatchesDomain(target.Host(), rule)) {
            matched = true;
            break;
        }
    }

    if (config.Mode() == FilterMode::Denylist) {
        return matched ? SecurityReason::DomainDenied : SecurityReason::Allowed;
    }

    return matched ? SecurityReason::Allowed : SecurityReason::DomainNotAllowed;
}

}  // namespace sbp
