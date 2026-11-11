#pragma once

#include <cstdint>

namespace sbp {

// Identifica el contexto local de sesión e instalación al que pertenece una operación.
// No contiene credenciales ni forma parte de PolicyResponse.
class SecurityContext final {
public:
    SecurityContext(
        std::uint64_t generation,
        std::int64_t deviceId,
        std::int64_t companyId);

    // Identidad local que PolicyManager renovará al iniciar un contexto nuevo.
    // No es el ID del usuario, un JWT ni un identificador recibido del backend.
    [[nodiscard]] std::uint64_t Generation() const noexcept;

    // ID numérico del Device del backend, distinto del deviceIdentifier de la instalación.
    [[nodiscard]] std::int64_t DeviceId() const noexcept;

    // Compañía obtenida del contexto autorizado, contra la que se validará la política.
    [[nodiscard]] std::int64_t CompanyId() const noexcept;

    // Dos contextos coinciden únicamente si son iguales sus tres identificadores.
    [[nodiscard]] bool operator==(const SecurityContext& other) const noexcept;

private:
    std::uint64_t generation_;
    std::int64_t deviceId_;
    std::int64_t companyId_;
};

}  // namespace sbp
