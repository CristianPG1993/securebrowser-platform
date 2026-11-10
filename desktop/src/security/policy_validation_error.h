#pragma once

namespace sbp {

// Identifica el resultado de validar los datos de una política antes de publicarla.
// No representa decisiones sobre una navegación o descarga ni errores de transporte REST.
enum class PolicyValidationError {
    // No se ha detectado ningún incumplimiento en los datos comprobados.
    None,

    // El identificador de la política no es positivo.
    InvalidPolicyId,

    // El identificador de la compañía no es positivo.
    InvalidCompanyId,

    // La compañía de la política no corresponde a la instalación solicitante.
    CompanyMismatch,

    // El nombre incumple el contrato de contenido, codificación o longitud.
    InvalidName,

    // El modo de URL Filtering no es uno de los valores admitidos.
    InvalidUrlFilteringMode,

    // El modo de Download Control no es uno de los valores admitidos.
    InvalidDownloadControlMode,

    // Una regla de dominio incumple el contrato de la política.
    InvalidUrlRule,

    // Dos reglas de dominio coinciden tras su normalización.
    DuplicateUrlRule,

    // Una regla de extensión incumple el contrato de la política.
    InvalidDownloadRule,

    // Dos reglas de extensión coinciden tras su normalización.
    DuplicateDownloadRule,

};

}  // namespace sbp
