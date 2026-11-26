#pragma once

#include "security/security_event.h"

#include <QByteArray>
#include <QString>

namespace sbp {

// Prepara el cuerpo de POST /devices/{id}/events/batch con un evento ya producido.
class SecurityEventSerializer final {
public:
    [[nodiscard]] static QByteArray Serialize(const SecurityEvent& event);

private:
    static QString TypeName(SecurityEventType type);
};

}  // namespace sbp
