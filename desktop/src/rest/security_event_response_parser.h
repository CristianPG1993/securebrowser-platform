#pragma once

#include <QByteArray>

#include <string>

namespace sbp {

// Lee la confirmación del backend para un lote que Desktop envió con un único evento.
class SecurityEventResponseParser final {
public:
    [[nodiscard]] static bool ConfirmsEvent(const QByteArray& body, const std::string& sentUuid);
};

}  // namespace sbp
