#pragma once

#include <QString>

#include <cstdint>

namespace sbp {

// Representa los cuatro campos JSON que devuelve el backend tras login o refresh.
// El lector JSON comprobará campos y valores antes de aceptar esta respuesta como una sesión válida.
// Contiene secretos: conservar únicamente en memoria y no imprimir los tokens en logs.
struct TokenResponse {
    // JWT que se enviará en Authorization para consultar las rutas protegidas de la API.
    QString accessToken;

    // Esquema recibido del backend, actualmente Bearer; no se presupone si falta en el JSON.
    QString tokenType;

    // Segundos de validez del JWT desde su emisión, como Long en Kotlin y long en Java.
    // El cero inicial no representa una respuesta válida ni sustituye la validación del campo recibido.
    std::int64_t expiresIn = 0;

    // Secreto utilizado para renovar la sesión; el backend lo rota al entregar un nuevo par de tokens.
    QString refreshToken;
};

}  // namespace sbp
