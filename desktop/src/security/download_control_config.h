#pragma once

#include "security/filter_mode.h"

#include <string>
#include <vector>

namespace sbp {

// Agrupa los datos de Download Control sin evaluar archivos ni abrir diálogos.
// PolicyManager debe validar esta configuración antes de publicar la política.
class DownloadControlConfig final {
public:
    // Exige los tres datos de la configuración, incluso si el control está desactivado.
    DownloadControlConfig(bool enabled, FilterMode mode, std::vector<std::string> rules);

    // Indica si deben aplicarse las reglas de extensiones.
    [[nodiscard]] bool Enabled() const noexcept;

    // Devuelve cómo deben interpretarse las reglas.
    [[nodiscard]] FilterMode Mode() const noexcept;

    // Devuelve las extensiones UTF-8 como una colección de solo lectura.
    // La referencia solo es válida mientras se conserve esta configuración.
    [[nodiscard]] const std::vector<std::string>& Rules() const noexcept;

private:
    // Activación de las reglas, independiente de la disponibilidad de la política.
    bool enabled_;

    // Interpretación de la lista como extensiones bloqueadas o permitidas.
    FilterMode mode_;

    // Copia propia de las reglas recibidas, sin referencias a la respuesta REST.
    std::vector<std::string> rules_;
};

}  // namespace sbp
