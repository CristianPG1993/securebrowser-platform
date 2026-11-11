#pragma once

#include "security/download_control_config.h"
#include "security/url_filtering_config.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace sbp {

// Conserva una política completa; PolicyManager debe validarla antes de publicarla.
class PolicySnapshot final {
public:
    // Representa las fechas UTC del backend con precisión de microsegundos.
    // La caducidad local utilizará un reloj monotónico fuera de este objeto.
    using Timestamp = std::chrono::sys_time<std::chrono::microseconds>;

    // Exige los metadatos y ambas configuraciones completas de PolicyResponse.
    PolicySnapshot(
        std::int64_t id,
        std::int64_t companyId,
        std::string name,
        UrlFilteringConfig urlFiltering,
        DownloadControlConfig downloadControl,
        Timestamp createdAt,
        Timestamp updatedAt);

    // Devuelve el identificador de la política.
    [[nodiscard]] std::int64_t Id() const noexcept;

    // Devuelve la compañía propietaria, que el gestor comprobará contra el contexto.
    [[nodiscard]] std::int64_t CompanyId() const noexcept;

    // Devuelve el nombre UTF-8 sin permitir modificarlo a través de la referencia.
    // Las referencias devueltas solo son válidas mientras se conserve este snapshot.
    [[nodiscard]] const std::string& Name() const noexcept;

    // Devuelve la configuración de URL Filtering como un objeto de solo lectura.
    [[nodiscard]] const UrlFilteringConfig& UrlFiltering() const noexcept;

    // Devuelve la configuración de Download Control como un objeto de solo lectura.
    [[nodiscard]] const DownloadControlConfig& DownloadControl() const noexcept;

    // Devuelve la fecha de creación de la política en el backend.
    [[nodiscard]] Timestamp CreatedAt() const noexcept;

    // Devuelve la fecha de modificación; no representa la vigencia local.
    [[nodiscard]] Timestamp UpdatedAt() const noexcept;

private:
    // Identidad de la política y de la compañía propietaria.
    std::int64_t id_;
    std::int64_t companyId_;

    // Metadato descriptivo, independiente de las decisiones de seguridad.
    std::string name_;

    // Configuración completa de URL Filtering.
    UrlFilteringConfig urlFiltering_;

    // Configuración completa de Download Control.
    DownloadControlConfig downloadControl_;

    // Auditoría recibida del backend; no contiene datos de sesión o instalación.
    Timestamp createdAt_;
    Timestamp updatedAt_;
};

}  // namespace sbp
