#include "rest/device_page_parser.h"

#include "rest/device_identity_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <algorithm>
#include <cstdint>
#include <utility>

namespace sbp {

// Interpreta una página de GET /devices y devuelve las identidades en el orden recibido.
std::optional<DevicePage> DevicePageParser::Parse(const QByteArray& body) {
    const QJsonDocument document = QJsonDocument::fromJson(body);
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    const QJsonValue itemsValue = object.value(QStringLiteral("items"));

    // El valor de reserva -1 permite detectar campos ausentes, de otro tipo o fuera del rango entero.
    const int page = object.value(QStringLiteral("page")).toInt(-1);
    const int size = object.value(QStringLiteral("size")).toInt(-1);
    const std::int64_t totalElements = object.value(QStringLiteral("totalElements")).toInteger(-1);
    const int totalPages = object.value(QStringLiteral("totalPages")).toInt(-1);
    if (!itemsValue.isArray() || page < 0 || size < 1 || size > 100 || totalElements < 0 || totalPages < 0) {
        return std::nullopt;
    }

    // Una última página parcial también cuenta. Una página posterior a la última tiene cero elementos.
    const auto expectedPages = totalElements / size + (totalElements % size != 0);
    const auto offset = static_cast<std::int64_t>(page) * size;
    const auto expectedItems = offset >= totalElements ? 0 : std::min<std::int64_t>(size, totalElements - offset);
    const QJsonArray items = itemsValue.toArray();
    if (totalPages != expectedPages || items.size() != expectedItems) {
        return std::nullopt;
    }

    DevicePage result;
    result.page = page;
    result.totalPages = totalPages;
    for (const QJsonValue& item : items) {
        if (!item.isObject()) {
            return std::nullopt;
        }
        auto identity = DeviceIdentityParser::Parse(item.toObject());
        if (!identity) {
            return std::nullopt;
        }
        result.items.push_back(std::move(*identity));
    }
    return result;
}

}  // namespace sbp
