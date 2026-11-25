#pragma once

#include "rest/device_identity.h"

#include <QJsonObject>

#include <optional>

namespace sbp {

// Lee un dispositivo de la lista items que devuelve GET /devices.
class DeviceIdentityParser final {
public:
    // Devuelve la identidad si los cinco campos son válidos. Un dispositivo inactivo conserva active = false.
    // Si falta un campo o su valor es inválido, devuelve std::nullopt.
    [[nodiscard]] static std::optional<DeviceIdentity> Parse(const QJsonObject& item);
};

}  // namespace sbp
