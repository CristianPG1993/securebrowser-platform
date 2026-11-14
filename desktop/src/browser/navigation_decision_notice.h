#pragma once

#include "security/security_context.h"
#include "security/security_decision.h"

#include <cstdint>
#include <chrono>
#include <optional>
#include <string>

namespace sbp {

// Copia propia del bloqueo aplicado. No contiene URL, query, credenciales ni objetos CEF.
// operationId es único durante el proceso; el productor de eventos asignará su UUID persistente.
struct NavigationDecisionNotice {
    std::uint64_t operationId = 0;
    std::chrono::sys_time<std::chrono::microseconds> occurredAt{};
    int browserId = 0;
    std::string frameId;
    bool mainFrame = false;
    bool redirect = false;
    std::optional<SecurityContext> context;
    std::optional<std::int64_t> policyId;
    std::string host;
    SecurityDecision decision{SecurityReason::InvalidContext};
};

}  // namespace sbp
