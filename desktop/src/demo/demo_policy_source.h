#pragma once

#include "security/policy_snapshot.h"

#include <cstdint>

namespace sbp {

// Construye datos locales de demostración; no evalúa destinos ni publica políticas.
// La composición solo debe utilizar esta fuente cuando el modo demo esté activo.
class DemoPolicySource final {
public:
    DemoPolicySource() = delete;

    // Identificadores sintéticos confinados a la sesión demo, sin correspondencia con el backend.
    static constexpr std::int64_t DeviceId = 1;
    static constexpr std::int64_t CompanyId = 1;

    // Construye «Navegación libre», con filtrado de dominios y control de extensiones desactivados.
    [[nodiscard]] static PolicySnapshot CreateFreePolicy();

    // Construye la política restrictiva con filtrado de dominios activo y DENYLIST de descargas exe y msi.
    [[nodiscard]] static PolicySnapshot CreateRestrictedPolicy();
};

}  // namespace sbp
