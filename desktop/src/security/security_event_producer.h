#pragma once

#include "security/security_event_sink.h"
#include "security/security_reason.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace sbp {

// Crea el evento a partir de un bloqueo ya aplicado y lo entrega con su contexto de origen.
class SecurityEventProducer final {
public:
    using UuidGenerator = std::function<std::string()>;

    // El generador proporciona un UUID por evento. El receptor decide si acepta tramitarlo.
    SecurityEventProducer(UuidGenerator generateUuid, SecurityEventSink sink);

    // Recibe la hora del bloqueo y el host ya interpretado. Devuelve true si el receptor acepta el evento.
    [[nodiscard]] bool ProduceUrlBlocked(const SecurityContext& origin,
                                         std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                                         SecurityReason reason, std::string_view host,
                                         std::optional<std::int64_t> policyId) const noexcept;

    // La extensión procede de la evaluación del nombre sugerido o del destino definitivo.
    [[nodiscard]] bool ProduceDownloadBlocked(const SecurityContext& origin,
                                              std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                                              SecurityReason reason, std::string_view extension,
                                              std::optional<std::int64_t> policyId) const noexcept;

private:
    bool ProduceBlocked(SecurityEventType type, const SecurityContext& origin,
                        std::chrono::sys_time<std::chrono::microseconds> occurredAt,
                        std::string_view reasonCode, std::string_view value,
                        std::optional<std::int64_t> policyId) const noexcept;
    static std::string_view ContextReasonCode(SecurityReason reason) noexcept;
    static std::string_view UrlBlockReasonCode(SecurityReason reason) noexcept;
    static std::string_view DownloadBlockReasonCode(SecurityReason reason) noexcept;
    static bool CanIncludeHost(std::string_view host) noexcept;
    static bool CanIncludeExtension(std::string_view extension, std::size_t maxLength) noexcept;
    static bool IsValidEventUuid(std::string_view uuid) noexcept;

    UuidGenerator generateUuid_;
    SecurityEventSink sink_;
};

}  // namespace sbp
