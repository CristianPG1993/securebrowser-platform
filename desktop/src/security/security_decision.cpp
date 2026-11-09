#include "security/security_decision.h"

namespace sbp {

// Inicializa el motivo al construir la decisión, antes de ejecutar el cuerpo.
SecurityDecision::SecurityDecision(SecurityReason reason)
    : reason_(reason) {}

// Solo el motivo Allowed autoriza la operación; los demás producen bloqueo.
SecurityAction SecurityDecision::Action() const noexcept {
    if (reason_ == SecurityReason::Allowed) {
        return SecurityAction::Allow;
    }

    return SecurityAction::Block;
}

// Entrega una copia del motivo sin modificar el estado de la decisión.
SecurityReason SecurityDecision::Reason() const noexcept {
    return reason_;
}

}  // namespace sbp
