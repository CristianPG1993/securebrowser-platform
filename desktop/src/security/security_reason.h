#pragma once

namespace sbp {

// Identifica el motivo de una decisión sin depender de mensajes de interfaz.
enum class SecurityReason {
    // La operación supera las comprobaciones aplicables.
    Allowed,

    // La operación no pertenece a un contexto de sesión e instalación válido.
    InvalidContext,

    // La instalación no está autorizada para realizar operaciones protegidas.
    InstallationUnauthorized,

    // No hay una política válida disponible para evaluar la operación.
    PolicyUnavailable,

    // La política ha agotado su vigencia local.
    PolicyExpired,

    // La URL HTTP/HTTPS no puede interpretarse de forma válida.
    InvalidUrl,

    // El esquema no está admitido como destino de navegación.
    UnsupportedScheme,

    // El destino es un literal IP bloqueado por el filtrado.
    IpBlocked,

    // El dominio coincide con una regla de DENYLIST.
    DomainDenied,

    // El dominio no coincide con ninguna regla de ALLOWLIST.
    DomainNotAllowed,

    // El nombre del archivo no es válido para guardarse.
    InvalidFileName,

    // El archivo carece de extensión con Download Control activado.
    MissingExtension,

    // La extensión coincide con una regla de DENYLIST.
    ExtensionDenied,

    // La extensión no coincide con ninguna regla de ALLOWLIST.
    ExtensionNotAllowed
};

}  // namespace sbp
