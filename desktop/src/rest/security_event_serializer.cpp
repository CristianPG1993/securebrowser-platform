#include "rest/security_event_serializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <format>
#include <stdexcept>

namespace sbp {

QByteArray SecurityEventSerializer::Serialize(const SecurityEvent& event) {
    // occurredAt es un instante UTC con precisión de microsegundos. Z indica UTC en el contrato REST.
    const auto occurredAt = std::format("{:%FT%T}Z", event.occurredAt);
    const QJsonObject item{
        {QStringLiteral("eventUuid"), QString::fromStdString(event.eventUuid)},
        {QStringLiteral("type"), TypeName(event.type)},
        {QStringLiteral("occurredAt"), QString::fromStdString(occurredAt)},
        // Qt escapa las comillas de details al escribir la cadena dentro del JSON exterior.
        {QStringLiteral("details"), event.details ? QJsonValue(QString::fromStdString(*event.details))
                                                  : QJsonValue(QJsonValue::Null)}
    };
    const QJsonObject batch{{QStringLiteral("events"), QJsonArray{item}}};
    return QJsonDocument(batch).toJson(QJsonDocument::Compact);
}

QString SecurityEventSerializer::TypeName(SecurityEventType type) {
    switch (type) {
    case SecurityEventType::UrlBlocked: return QStringLiteral("URL_BLOCKED");
    case SecurityEventType::DownloadBlocked: return QStringLiteral("DOWNLOAD_BLOCKED");
    case SecurityEventType::PolicyUpdated: return QStringLiteral("POLICY_UPDATED");
    }
    throw std::invalid_argument("Unknown security event type.");
}

}  // namespace sbp
