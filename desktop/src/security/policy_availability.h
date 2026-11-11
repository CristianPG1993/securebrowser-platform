#pragma once

namespace sbp {

// Describe la disponibilidad de la política para un contexto, sin evaluar una URL o descarga.
enum class PolicyAvailability {
    // No existe un contexto actual o el consultado no coincide con él.
    InvalidContext,

    // Existe el contexto, pero todavía no hay una política con validación correcta registrada.
    Unavailable,

    // Se han alcanzado los 15 minutos desde la última obtención y validación correctas.
    Expired,

    // Existe una política válida y vigente para el contexto consultado.
    Available
};

}  // namespace sbp
