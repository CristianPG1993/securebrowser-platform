#pragma once

#include <cstdint>

namespace sbp {

// Identificadores que Desktop utiliza del perfil devuelto por GET /users/me.
struct UserIdentity {
    // Usuario autenticado. Corresponde al campo userId de sus dispositivos.
    std::int64_t id = 0;

    // Empresa a la que pertenece el usuario.
    std::int64_t companyId = 0;
};

}  // namespace sbp
