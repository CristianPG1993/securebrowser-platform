#pragma once

#include "auth_refresh_test.h"

namespace sbp {

// Reutiliza el login de prueba y el servidor local que retiene cada respuesta hasta que el test la envía.
class AuthDevicePolicyTest : public AuthRefreshTest {
protected:
    struct PolicyResult {
        int calls = 0;
        AuthResult result;
        std::optional<PolicySnapshot> policy;
    };

    // Guarda la entrega de FetchDevicePolicy y comprueba que el callback recibe el contexto original.
    [[nodiscard]] std::shared_ptr<PolicyResult> FetchPolicy(SecurityContext context);

    // Espera una sola entrega con el error indicado y la política vacía.
    static void ExpectPolicyError(const std::shared_ptr<PolicyResult>& reply, AuthOutcome outcome,
                                  std::optional<int> status, RestTransportError transport = RestTransportError::None);
};

}  // namespace sbp
