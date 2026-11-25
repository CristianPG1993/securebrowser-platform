#include "rest/token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

namespace sbp {

std::optional<TokenResponse> TokenResponseParser::Parse(const QByteArray& body) {
    // Interpreta los bytes UTF-8 recibidos. Un JSON mal formado o una raíz distinta de objeto se rechazan completos.
    // No se registra el cuerpo ni el detalle del error, porque la respuesta puede contener tokens.
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    const QJsonValue accessTokenValue = object.value(QStringLiteral("accessToken"));
    const QJsonValue tokenTypeValue = object.value(QStringLiteral("tokenType"));
    const QJsonValue expiresInValue = object.value(QStringLiteral("expiresIn"));
    const QJsonValue refreshTokenValue = object.value(QStringLiteral("refreshToken"));

    // Un campo ausente es Undefined y uno null tiene su propio tipo: ambos fallan estas comprobaciones.
    // isDouble identifica los números JSON de Qt, incluidos los almacenados internamente como enteros de 64 bits.
    if (!accessTokenValue.isString() || !tokenTypeValue.isString()
        || !expiresInValue.isDouble() || !refreshTokenValue.isString()) {
        return std::nullopt;
    }

    // Solo se convierten valores del tipo correcto; una cadena "900" no sustituye a un número JSON.
    const QString accessToken = accessTokenValue.toString();
    const QString tokenType = tokenTypeValue.toString();
    const QString refreshToken = refreshTokenValue.toString();
    // toInteger devuelve 0 si hay fracción o el número no cabe en 64 bits; ese cero se rechaza más abajo.
    const std::int64_t expiresIn = expiresInValue.toInteger(0);

    // Las mismas condiciones básicas que Android: tokens no vacíos, esquema Bearer y duración positiva.
    // trimmed solo comprueba si hay contenido: no modifica los tokens que se devolverán.
    if (accessToken.trimmed().isEmpty() || refreshToken.trimmed().isEmpty()
        || tokenType.compare(QStringLiteral("Bearer"), Qt::CaseInsensitive) != 0 || expiresIn <= 0) {
        return std::nullopt;
    }

    // Publica los cuatro campos juntos. Esto valida el DTO; la firma y los permisos los verifica el backend.
    return TokenResponse{accessToken, tokenType, expiresIn, refreshToken};
}

}  // namespace sbp
