#pragma once

#include "security/filter_mode.h"
#include "security/policy_snapshot.h"

#include <QByteArray>
#include <QJsonValue>

#include <optional>
#include <string>
#include <vector>

namespace sbp {

// Lee la política asignada que devuelve GET /devices/{id}/policy.
class PolicyResponseParser final {
public:
    // Convierte el JSON completo en el snapshot que utiliza PolicyManager.
    // Si falta un campo obligatorio o su formato es incorrecto, devuelve std::nullopt.
    [[nodiscard]] static std::optional<PolicySnapshot> Parse(const QByteArray& body);

private:
    // Ambos campos de modo del JSON utilizan los mismos dos valores del backend.
    [[nodiscard]] static std::optional<FilterMode> ParseMode(const QJsonValue& value);

    // Lee urlRules o downloadRules en orden. Un array vacío representa una lista válida sin reglas.
    [[nodiscard]] static std::optional<std::vector<std::string>> ParseRules(const QJsonValue& value);

    // Lee las fechas UTC de auditoría del backend con la precisión de microsegundos del snapshot.
    [[nodiscard]] static std::optional<PolicySnapshot::Timestamp> ParseTimestamp(const QJsonValue& value);
};

}  // namespace sbp
