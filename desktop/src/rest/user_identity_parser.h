#pragma once

#include "rest/user_identity.h"

#include <QByteArray>

#include <optional>

namespace sbp {

// Lee los identificadores del cuerpo JSON que devuelve GET /users/me.
class UserIdentityParser final {
public:
    // Devuelve la identidad cuando id y companyId son enteros positivos de 64 bits.
    // Si el JSON es inválido o falta alguno de los dos identificadores, devuelve std::nullopt.
    [[nodiscard]] static std::optional<UserIdentity> Parse(const QByteArray& body);
};

}  // namespace sbp
