#pragma once

#include "security/download_control_config.h"
#include "security/policy_manager.h"
#include "security/security_decision.h"

#include <cstddef>
#include <string>

namespace sbp {

// Evalúa un nombre de archivo sin acceder al disco, abrir diálogos ni emitir eventos.
class DownloadPolicy final {
public:
    // Recibe una lectura reciente de PolicyManager::ReadPolicy para la operación solicitante.
    // fileName contiene el nombre UTF-8 sugerido o definitivo, separado de la carpeta.
    // Comprueba disponibilidad, validez del nombre Windows y reglas, conservando el primer bloqueo.
    // Con el control activo, evalúa la última extensión en minúsculas Unicode y sin punto.
    // Con el control desactivado, omite listas y ausencia de extensión, pero valida el nombre.
    // No corrige el nombre, consulta el reloj ni renueva la política.
    // El resultado no sustituye la comprobación final de contexto y autorización al aplicarlo.
    [[nodiscard]] SecurityDecision Evaluate(
        const PolicyManager::PolicyReadResult& policy,
        const std::string& fileName) const;

private:
    // Comprueba contexto y disponibilidad en la lectura recibida, sin consultar el reloj.
    // Allowed permite continuar con la validación del nombre; todavía no autoriza la descarga.
    [[nodiscard]] static SecurityReason CheckPolicyAvailability(
        const PolicyManager::PolicyReadResult& policy);

    // Comprueba UTF-8 y validez del nombre Windows, sin modificarlo ni acceder al disco.
    // Se aplica también con el control desactivado, antes de evaluar extensiones y listas.
    // Devuelve InvalidFileName si es inválido; Allowed permite continuar con las reglas.
    [[nodiscard]] static SecurityReason CheckFileName(const std::string& fileName);

    // Identifica nombres de dispositivos reservados de Windows, incluso con extensión.
    // La comprobación no distingue mayúsculas ASCII ni modifica el nombre recibido.
    // Devuelve true para nombres reservados como NUL.txt o COM1.exe.
    [[nodiscard]] static bool IsReservedFileName(const std::string& fileName);

    // Extrae la última extensión sin el punto de un nombre que ya superó CheckFileName.
    // Devuelve una cadena vacía si no hay extensión, incluidos informe y .env.
    // Conserva las mayúsculas y Unicode; la conversión a minúsculas se realiza después.
    [[nodiscard]] static std::string ExtractExtension(const std::string& fileName);

    // Convierte UTF-8 válido a minúsculas Unicode con el locale raíz de ICU.
    // No elimina acentos ni equipara caracteres distintos por su parecido visual.
    // Devuelve false ante UTF-8 inválido o un fallo de conversión, sin modificar lowercased.
    [[nodiscard]] static bool ToLowerUtf8(const std::string& value, std::string& lowercased);

    // Lee un punto de código UTF-8 válido desde la posición en bytes indicada por offset.
    // Actualiza offset y codePoint solo si la lectura tiene éxito.
    // Devuelve false si no hay un carácter completo válido en esa posición.
    [[nodiscard]] static bool ReadUtf8CodePoint(
        const std::string& value, std::size_t& offset, char32_t& codePoint) noexcept;

    // Recibe un nombre ya validado y una configuración de la política publicada por PolicyManager.
    // Rechaza modos desconocidos incluso con el control desactivado.
    // Con el control activo, comprueba ausencia de extensión y aplica DENYLIST o ALLOWLIST.
    // Con el control desactivado, omite listas y la restricción de ausencia de extensión.
    [[nodiscard]] static SecurityReason EvaluateRules(
        const DownloadControlConfig& config, const std::string& fileName);
};

}  // namespace sbp
