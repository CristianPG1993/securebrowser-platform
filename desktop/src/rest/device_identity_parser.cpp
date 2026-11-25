#include "rest/device_identity_parser.h"

#include <QJsonValue>
#include <QUuid>

namespace sbp {

// Lee los cinco campos de un elemento de GET /devices antes de construir su identidad.
std::optional<DeviceIdentity> DeviceIdentityParser::Parse(const QJsonObject& item) {
    const QJsonValue idValue = item.value(QStringLiteral("id"));
    const QJsonValue identifierValue = item.value(QStringLiteral("deviceIdentifier"));
    const QJsonValue userIdValue = item.value(QStringLiteral("userId"));
    const QJsonValue companyIdValue = item.value(QStringLiteral("companyId"));
    const QJsonValue activeValue = item.value(QStringLiteral("active"));

    // En Qt, isDouble reconoce los números JSON, incluidos los enteros de 64 bits.
    if (!idValue.isDouble() || !identifierValue.isString() || !userIdValue.isDouble()
        || !companyIdValue.isDouble() || !activeValue.isBool()) {
        return std::nullopt;
    }

    // toInteger devuelve el valor de reserva 0 para una fracción o un número fuera de rango.
    const std::int64_t id = idValue.toInteger(0);
    const std::int64_t userId = userIdValue.toInteger(0);
    const std::int64_t companyId = companyIdValue.toInteger(0);
    if (id <= 0 || userId <= 0 || companyId <= 0) {
        return std::nullopt;
    }

    // El backend devuelve el UUID en minúsculas, con guiones y sin llaves.
    const QString deviceIdentifier = identifierValue.toString();
    if (QUuid::fromString(deviceIdentifier).toString(QUuid::WithoutBraces) != deviceIdentifier) {
        return std::nullopt;
    }

    return DeviceIdentity{id, deviceIdentifier, userId, companyId, activeValue.toBool()};
}

}  // namespace sbp
