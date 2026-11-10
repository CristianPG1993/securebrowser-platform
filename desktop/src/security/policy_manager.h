#pragma once

#include "security/policy_snapshot.h"
#include "security/policy_validation_error.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace sbp {

// Gestionará la política local; esta primera parte declara su validación semántica.
class PolicyManager final {
public:
    // Comprueba el conjunto y devuelve el primer error, o None si todos los datos son válidos.
    // La compañía esperada debe proceder del contexto de instalación, no de la política.
    // REST debe exigir las fechas y convertirlas correctamente a Timestamp antes de construir el snapshot.
    // Validar no publica el snapshot ni acredita por sí solo sesión, autorización o vigencia.
    [[nodiscard]] PolicyValidationError Validate(
        const PolicySnapshot& policy, std::int64_t expectedCompanyId) const;

private:
    // Comprueba identificadores positivos y correspondencia de la compañía.
    [[nodiscard]] static PolicyValidationError ValidateIdentity(
        const PolicySnapshot& policy, std::int64_t expectedCompanyId) noexcept;

    // Comprueba codificación, contenido y longitud del nombre según el contrato backend.
    [[nodiscard]] static PolicyValidationError ValidateName(const std::string& name);

    // Exige modos conocidos incluso cuando las funcionalidades estén desactivadas.
    [[nodiscard]] static PolicyValidationError ValidateModes(const PolicySnapshot& policy) noexcept;

    // Comprueba todas las reglas de dominio y rechaza duplicados normalizados.
    [[nodiscard]] static PolicyValidationError ValidateUrlRules(const UrlFilteringConfig& config);

    // Comprueba todas las reglas de extensión y rechaza duplicados normalizados.
    [[nodiscard]] static PolicyValidationError ValidateDownloadRules(const DownloadControlConfig& config);

    // Convierte UTF-8 válido a minúsculas Unicode con el locale raíz de ICU.
    // Solo modifica lowercased si la conversión tiene éxito; no elimina acentos.
    [[nodiscard]] static bool ToLowerUtf8(const std::string& value, std::string& lowercased);

    // Lee un punto de código UTF-8 válido y avanza offset solo si la lectura tiene éxito.
    // Devuelve false si no hay un carácter completo válido en la posición indicada.
    [[nodiscard]] static bool ReadUtf8CodePoint(
        const std::string& value, std::size_t& offset, char32_t& codePoint) noexcept;
};

}  // namespace sbp
