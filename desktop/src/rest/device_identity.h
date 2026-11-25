#pragma once

#include <QString>

#include <cstdint>

namespace sbp {

// Campos de GET /devices que permiten reconocer la instalación en la lista de dispositivos del usuario.
struct DeviceIdentity {
    // ID numérico del dispositivo en el backend. Se usa en GET /devices/{id}/policy.
    std::int64_t id = 0;

    // UUID estable que Desktop compara con el de su instalación.
    QString deviceIdentifier;

    // ID del propietario que debe coincidir con el id del perfil de GET /users/me.
    std::int64_t userId = 0;

    // ID de la empresa que debe coincidir con el companyId del perfil.
    std::int64_t companyId = 0;

    // Indica si el backend tiene activo el dispositivo.
    bool active = false;
};

}  // namespace sbp
