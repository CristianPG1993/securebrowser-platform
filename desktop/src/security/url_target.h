#pragma once

#include "security/url_host_kind.h"

#include <string>

namespace sbp {

// Conserva los datos de un destino ya interpretado por el adaptador de URL.
// No analiza la URL original ni acredita autorización o disponibilidad de la política.
class UrlTarget final {
public:
    UrlTarget(bool valid, std::string scheme, std::string host, UrlHostKind hostKind);

    // Indica si el adaptador pudo interpretar el destino de forma válida.
    // No significa que su esquema o su host estén permitidos por el motor.
    [[nodiscard]] bool IsValid() const noexcept;

    // Esquema en minúsculas y sin los dos puntos, por ejemplo "https".
    [[nodiscard]] const std::string& Scheme() const noexcept;

    // Host preparado por el adaptador; los dominios llegan en ASCII,
    // en minúsculas y sin un punto final, conservando su clasificación previa.
    [[nodiscard]] const std::string& Host() const noexcept;

    // Distingue dominio, IPv4, IPv6 y host sin clasificar.
    [[nodiscard]] UrlHostKind HostKind() const noexcept;

private:
    bool valid_;
    std::string scheme_;
    std::string host_;
    UrlHostKind hostKind_;
};

}  // namespace sbp
