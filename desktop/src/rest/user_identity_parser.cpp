#include "rest/user_identity_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

namespace sbp {

std::optional<UserIdentity> UserIdentityParser::Parse(const QByteArray& body) {
    // GET /users/me devuelve el perfil como un objeto JSON. Un cuerpo mal formado produce un documento nulo.
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject profile = document.object();
    const QJsonValue idValue = profile.value(QStringLiteral("id"));
    const QJsonValue companyIdValue = profile.value(QStringLiteral("companyId"));
    // isDouble comprueba el tipo numérico de JSON en Qt, incluidos los enteros de 64 bits.
    if (!idValue.isDouble() || !companyIdValue.isDouble()) {
        return std::nullopt;
    }

    // toInteger(0) devuelve cero si el número tiene parte fraccionaria o queda fuera del rango entero.
    const std::int64_t id = idValue.toInteger(0);
    const std::int64_t companyId = companyIdValue.toInteger(0);
    if (id <= 0 || companyId <= 0) {
        return std::nullopt;
    }

    return UserIdentity{id, companyId};
}

}  // namespace sbp
