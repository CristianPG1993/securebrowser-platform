#pragma once

#include "security/security_context.h"
#include "security/security_decision.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace sbp {

// Copia propia del bloqueo aplicado a una descarga, sin rutas locales, URL ni objetos CEF.
// Permite comunicar el motivo a Qt y correlacionar el aviso sin repetirlo por cada actualización.
struct DownloadDecisionNotice {
    // Identificador propio de la operación; se conserva al pasar del nombre sugerido al definitivo.
    std::uint64_t operationId = 0;
    std::chrono::sys_time<std::chrono::microseconds> occurredAt{};
    int browserId = 0;

    // CanDownload no recibe el ID nativo; estará disponible al observar CefDownloadItem.
    std::optional<std::uint32_t> downloadId;

    // Conserva el origen y la política de la decisión; no se sustituyen por los de otra sesión posterior.
    std::optional<SecurityContext> context;
    std::optional<std::int64_t> policyId;
    SecurityDecision decision{SecurityReason::InvalidContext};
    // Solo las reglas de extensión proporcionan este dato, ya normalizado por DownloadPolicy.
    std::string extension;
};

}  // namespace sbp
