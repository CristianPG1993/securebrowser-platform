#include "demo/demo_policy_source.h"

#include <chrono>

namespace sbp {

PolicySnapshot DemoPolicySource::CreateFreePolicy() {
    // Son metadatos locales UTC; PolicyManager calculará la vigencia con su reloj monotónico.
    const auto timestamp = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now());

    // El nombre utiliza bytes UTF-8 explícitos, independientemente de la página de códigos de MSVC.
    return PolicySnapshot(
        1, CompanyId, "Navegaci\xC3\xB3n libre",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        timestamp, timestamp);
}

PolicySnapshot DemoPolicySource::CreateRestrictedPolicy() {
    // Igual que en la política libre, estas fechas son metadatos y no controlan la caducidad.
    const auto timestamp = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now());

    // E1 utiliza una regla mínima; E2 completará los dominios definitivos de la demostración.
    return PolicySnapshot(
        2, CompanyId, "Restricci\xC3\xB3n de redes sociales y entretenimiento",
        UrlFilteringConfig(true, FilterMode::Denylist, {"youtube.com"}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        timestamp, timestamp);
}

}  // namespace sbp
