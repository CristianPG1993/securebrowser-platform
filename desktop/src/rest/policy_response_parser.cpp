#include "rest/policy_response_parser.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QString>
#include <QTimeZone>

#include <chrono>
#include <cstdint>
#include <utility>

namespace sbp {

// Lee los once campos de PolicyResponse antes de construir la política asignada al dispositivo.
std::optional<PolicySnapshot> PolicyResponseParser::Parse(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject policy = document.object();
    // toInteger(0) permite rechazar un ID ausente, de otro tipo, fraccionario o fuera de rango.
    const std::int64_t id = policy.value(QStringLiteral("id")).toInteger(0);
    const std::int64_t companyId = policy.value(QStringLiteral("companyId")).toInteger(0);
    const QString name = policy.value(QStringLiteral("name")).toString();
    if (id <= 0 || companyId <= 0 || name.isEmpty()) {
        return std::nullopt;
    }

    const QJsonValue urlEnabled = policy.value(QStringLiteral("urlFilteringEnabled"));
    const QJsonValue downloadEnabled = policy.value(QStringLiteral("downloadControlEnabled"));
    if (!urlEnabled.isBool() || !downloadEnabled.isBool()) {
        return std::nullopt;
    }

    // Ambas configuraciones requieren modo y lista de reglas incluso si su control está desactivado.
    const auto urlMode = ParseMode(policy.value(QStringLiteral("urlFilteringMode")));
    auto urlRules = ParseRules(policy.value(QStringLiteral("urlRules")));
    const auto downloadMode = ParseMode(policy.value(QStringLiteral("downloadControlMode")));
    auto downloadRules = ParseRules(policy.value(QStringLiteral("downloadRules")));
    if (!urlMode || !urlRules || !downloadMode || !downloadRules) {
        return std::nullopt;
    }

    const auto createdAt = ParseTimestamp(policy.value(QStringLiteral("createdAt")));
    const auto updatedAt = ParseTimestamp(policy.value(QStringLiteral("updatedAt")));
    if (!createdAt || !updatedAt) {
        return std::nullopt;
    }

    return PolicySnapshot(id, companyId, name.toStdString(),
                          UrlFilteringConfig(urlEnabled.toBool(), *urlMode, std::move(*urlRules)),
                          DownloadControlConfig(downloadEnabled.toBool(), *downloadMode, std::move(*downloadRules)),
                          *createdAt, *updatedAt);
}

// Traduce el modo de la API al valor que entiende el motor para URL Filtering y Download Control.
std::optional<FilterMode> PolicyResponseParser::ParseMode(const QJsonValue& value) {
    // toString devuelve una cadena vacía si el campo falta o tiene otro tipo.
    const QString mode = value.toString();
    if (mode == QStringLiteral("ALLOWLIST")) {
        return FilterMode::Allowlist;
    }
    if (mode == QStringLiteral("DENYLIST")) {
        return FilterMode::Denylist;
    }
    return std::nullopt;
}

// Conserva las reglas tal como llegan. PolicyManager valida los dominios y las extensiones antes de publicarlos.
std::optional<std::vector<std::string>> PolicyResponseParser::ParseRules(const QJsonValue& value) {
    if (!value.isArray()) {
        return std::nullopt;
    }

    const QJsonArray array = value.toArray();
    std::vector<std::string> rules;
    for (const QJsonValue& rule : array) {
        if (!rule.isString()) {
            return std::nullopt;
        }
        rules.push_back(rule.toString().toStdString());
    }
    return rules;
}

// createdAt y updatedAt son instantes ISO 8601 con zona UTC explícita y fracción de segundo opcional.
std::optional<PolicySnapshot::Timestamp> PolicyResponseParser::ParseTimestamp(const QJsonValue& value) {
    // Captura fecha, hora y fracción por separado para conservar más precisión que los milisegundos de Qt.
    static const QRegularExpression pattern(QStringLiteral(
        R"(\A([0-9]{4}-[0-9]{2}-[0-9]{2})T([0-9]{2}:[0-9]{2}:[0-9]{2})(?:\.([0-9]{1,9}))?(?:Z|[+-]00:00)\z)"));
    const auto match = pattern.match(value.toString());
    if (!match.hasMatch()) {
        return std::nullopt;
    }

    const QDate date = QDate::fromString(match.captured(1), Qt::ISODate);
    const QTime time = QTime::fromString(match.captured(2), QStringLiteral("hh:mm:ss"));
    if (!date.isValid() || !time.isValid()) {
        return std::nullopt;
    }

    // .12 representa 120000 microsegundos. Si Java envía nanosegundos, se conservan las primeras seis cifras.
    const QString fraction = match.captured(3).left(6).leftJustified(6, QLatin1Char('0'));
    const QDateTime wholeSeconds(date, time, QTimeZone::UTC);
    return wholeSeconds.toStdSysSeconds() + std::chrono::microseconds{fraction.toInt()};
}

}  // namespace sbp
