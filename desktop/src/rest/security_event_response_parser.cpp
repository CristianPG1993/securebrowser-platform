#include "rest/security_event_response_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace sbp {

bool SecurityEventResponseParser::ConfirmsEvent(const QByteArray& body, const std::string& sentUuid) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    const QJsonArray accepted = document.object().value(QStringLiteral("acceptedEventUuids")).toArray();
    if (accepted.size() != 1 || !accepted.at(0).isString()) {
        return false;
    }

    // El backend devuelve el UUID con guiones y puede normalizar sus letras a minúsculas.
    const QString receivedUuid = accepted.at(0).toString();
    const QString canonicalUuid = QUuid::fromString(receivedUuid).toString(QUuid::WithoutBraces);
    return canonicalUuid.compare(receivedUuid, Qt::CaseInsensitive) == 0
        && canonicalUuid.compare(QString::fromStdString(sentUuid), Qt::CaseInsensitive) == 0;
}

}  // namespace sbp
