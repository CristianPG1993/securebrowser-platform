#pragma once

#include "security/security_action.h"
#include "security/security_reason.h"

namespace sbp {

// Representa una decisión cuya acción se obtiene del motivo para mantener su coherencia.
class SecurityDecision final {
public:
    // Crea una decisión con un motivo explícito, sin un resultado predeterminado.
    explicit SecurityDecision(SecurityReason reason);

    // Devuelve Allow para Allowed y Block para los motivos de bloqueo.
    [[nodiscard]] SecurityAction Action() const noexcept;

    // Devuelve el motivo controlado que justifica la decisión.
    [[nodiscard]] SecurityReason Reason() const noexcept;

private:
    // Conserva el motivo sin almacenar una acción independiente que pueda contradecirlo.
    SecurityReason reason_;
};

}  // namespace sbp
