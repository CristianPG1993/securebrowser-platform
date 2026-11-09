#pragma once

namespace sbp {

// Representa la acción resultante de una decisión de seguridad.
enum class SecurityAction {
    // Permite continuar la operación evaluada.
    Allow,

    // Impide continuar la operación evaluada.
    Block
};

}  // namespace sbp
