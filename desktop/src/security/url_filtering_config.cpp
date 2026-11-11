#include "security/url_filtering_config.h"

#include <utility>

namespace sbp {

// Inicializa los datos y transfiere al objeto el vector recibido por valor.
UrlFilteringConfig::UrlFilteringConfig(
    bool enabled, FilterMode mode, std::vector<std::string> rules)
    : enabled_(enabled),
      mode_(mode),
      rules_(std::move(rules)) {}

// Consulta la activación sin modificar la configuración ni evaluar un destino.
bool UrlFilteringConfig::Enabled() const noexcept {
    return enabled_;
}

// Consulta cómo deben interpretarse las reglas, sin evaluar su contenido.
FilterMode UrlFilteringConfig::Mode() const noexcept {
    return mode_;
}

// Devuelve las reglas de filtrado como un vector de solo lectura.
const std::vector<std::string>& UrlFilteringConfig::Rules() const noexcept {
    return rules_;
}

}  // namespace sbp
