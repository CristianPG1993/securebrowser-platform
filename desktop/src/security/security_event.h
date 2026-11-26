#pragma once

#include <chrono>
#include <optional>
#include <string>

namespace sbp {

// Tipos que el backend acepta como URL_BLOCKED, DOWNLOAD_BLOCKED y POLICY_UPDATED.
enum class SecurityEventType {
    UrlBlocked,
    DownloadBlocked,
    PolicyUpdated
};

// El productor fija estos datos al ocurrir el evento. El dispositivo de origen viaja por separado.
struct SecurityEvent {
    // El mismo evento conserva su UUID en cada intento de entrega.
    const std::string eventUuid;
    const SecurityEventType type;
    // Instante del suceso en UTC, con la precisión que admite PostgreSQL.
    const std::chrono::sys_time<std::chrono::microseconds> occurredAt;
    // Cuando hay detalle, contiene JSON compacto serializado como texto.
    const std::optional<std::string> details;
};

}  // namespace sbp
