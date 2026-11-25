#pragma once

#include "rest/device_identity.h"

#include <vector>

namespace sbp {

// Datos de una página de GET /devices que Desktop necesita para buscar su instalación.
struct DevicePage {
    // Dispositivos recibidos en el array JSON items de esta página.
    std::vector<DeviceIdentity> items;

    // Índice de la página recibida. La primera tiene índice 0.
    int page = 0;

    // Número total de páginas del listado. Vale 0 cuando no hay dispositivos.
    int totalPages = 0;
};

}  // namespace sbp
