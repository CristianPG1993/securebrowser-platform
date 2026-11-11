#pragma once

namespace sbp {

// Identifica el resultado de intentar publicar una política completa en el estado local.
enum class PolicyPublicationResult {
    // Se ha publicado la política y se ha registrado una validación correcta para su vigencia.
    Published,

    // El contexto solicitante no existe o ya no coincide con el actual.
    InvalidContext,

    // Los datos de la política no superan la validación del conjunto.
    InvalidPolicy
};

}  // namespace sbp
