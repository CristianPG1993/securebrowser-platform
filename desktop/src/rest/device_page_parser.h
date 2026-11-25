#pragma once

#include "rest/device_page.h"

#include <QByteArray>

#include <optional>

namespace sbp {

// Lee una página completa del cuerpo JSON de GET /devices.
class DevicePageParser final {
public:
    // Una página válida puede tener items vacío. Si la paginación o algún dispositivo
    // es inválido, devuelve std::nullopt y descarta la página completa.
    [[nodiscard]] static std::optional<DevicePage> Parse(const QByteArray& body);
};

}  // namespace sbp
