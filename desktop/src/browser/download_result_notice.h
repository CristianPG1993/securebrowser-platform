#pragma once

#include "security/security_context.h"

#include <cstdint>
#include <optional>

namespace sbp {

// Resultado de la operación, distinto de una decisión de permitir o bloquear por política.
enum class DownloadOutcome {
    Completed,
    DialogFailed,
    DestinationFailed,
    FileFailed,
    FileAccessDenied,
    FileNoSpace,
    FileBlocked,
    NetworkFailed,
    ServerFailed,
    Interrupted
};

// Copia propia para Qt, sin nombres, rutas, URL ni referencias a objetos CEF.
// No incluye policyId: una descarga autorizada puede terminar después de cambiar la política.
struct DownloadResultNotice {
    std::uint64_t operationId = 0;
    int browserId = 0;
    std::uint32_t downloadId = 0;
    std::optional<SecurityContext> context;
    DownloadOutcome outcome = DownloadOutcome::Interrupted;
};

}  // namespace sbp
