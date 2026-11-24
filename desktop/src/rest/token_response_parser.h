#pragma once

#include "rest/token_response.h"

#include <QByteArray>

#include <optional>

namespace sbp {

// Convierte el cuerpo JSON de login o refresh al modelo TokenResponse.
// El llamador comprobará antes el éxito HTTP y la ausencia de fallos de transporte.
class TokenResponseParser final {
public:
    // Devuelve una respuesta completa o nullopt si el JSON, los tipos o los valores no son válidos.
    // Los cuatro campos son obligatorios; no se completan con valores predeterminados ni tokens anteriores.
    // static permite utilizar el lector sin crear un objeto; no guarda tokens ni establece una sesión.
    [[nodiscard]] static std::optional<TokenResponse> Parse(const QByteArray& body);
};

}  // namespace sbp
