#pragma once

namespace sbp {

// Define cómo se interpretan las reglas de una funcionalidad de la política.
enum class FilterMode {
    // Las reglas identifican los elementos bloqueados.
    Denylist,

    // Las reglas identifican los elementos permitidos.
    Allowlist
};

}  // namespace sbp
