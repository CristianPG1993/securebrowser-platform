#include "security/url_target.h"

#include <utility>

namespace sbp {

// Conserva los datos del adaptador y transfiere las cadenas recibidas por valor.
UrlTarget::UrlTarget(bool valid, std::string scheme, std::string host, UrlHostKind hostKind)
    : valid_(valid),
      scheme_(std::move(scheme)),
      host_(std::move(host)),
      hostKind_(hostKind) {}

// Consulta el resultado de interpretación conservado, sin evaluar permisos.
bool UrlTarget::IsValid() const noexcept {
    return valid_;
}

// Devuelve el esquema de la URL. Ejemplo: "http" o "https".
const std::string& UrlTarget::Scheme() const noexcept {
    return scheme_;
}

// Devuelve el host de la URL. Ejemplo: "example.com" o "127.0.0.1".
const std::string& UrlTarget::Host() const noexcept {
    return host_;
}

// Devuelve la clasificación conservada: Unknown, Domain, Ipv4 o Ipv6.
UrlHostKind UrlTarget::HostKind() const noexcept {
    return hostKind_;
}

}  // namespace sbp
