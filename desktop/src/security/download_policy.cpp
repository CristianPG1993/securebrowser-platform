#include "security/download_policy.h"

#include <unicode/uchar.h>
#include <unicode/ucasemap.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

namespace sbp {

// Coordina las comprobaciones y conserva el primer motivo que impide continuar.
SecurityDecision DownloadPolicy::Evaluate(
    const PolicyManager::PolicyReadResult& policy, const std::string& fileName, std::string* blockedExtension) const {
    std::string extension;
    SecurityReason reason = CheckPolicyAvailability(policy);
    if (reason == SecurityReason::Allowed) {
        reason = CheckFileName(fileName);
    }
    if (reason == SecurityReason::Allowed) {
        reason = EvaluateRules(policy.state->policy->DownloadControl(), fileName, extension);
    }
    if (blockedExtension) {
        *blockedExtension = std::move(extension);
    }
    return SecurityDecision(reason);
}

// Rechaza lecturas sin contexto o política completa antes de evaluar el nombre del archivo.
SecurityReason DownloadPolicy::CheckPolicyAvailability(
    const PolicyManager::PolicyReadResult& policy) {
    if (policy.availability == PolicyAvailability::InvalidContext || !policy.state) {
        return SecurityReason::InvalidContext;
    }

    if (!policy.state->policy || !policy.state->validatedAt.has_value()) {
        return SecurityReason::PolicyUnavailable;
    }

    if (policy.availability == PolicyAvailability::Expired) {
        return SecurityReason::PolicyExpired;
    }

    // Solo Available supera el control; Unavailable y valores desconocidos se rechazan.
    if (policy.availability != PolicyAvailability::Available) {
        return SecurityReason::PolicyUnavailable;
    }

    return SecurityReason::Allowed;
}

// Comprueba el nombre completo sin modificarlo, también con Download Control desactivado.
SecurityReason DownloadPolicy::CheckFileName(const std::string& fileName) {
    if (fileName.empty() || fileName == "." || fileName == "..") {
        return SecurityReason::InvalidFileName;
    }

    // Comprueba el último byte solo después de descartar el nombre vacío.
    if (fileName.back() == '.' || fileName.back() == ' ') {
        return SecurityReason::InvalidFileName;
    }

    std::size_t offset = 0;
    while (offset < fileName.size()) {
        char32_t codePoint = 0;
        if (!ReadUtf8CodePoint(fileName, offset, codePoint)) {
            return SecurityReason::InvalidFileName;
        }

        // Incluye NUL y los controles Unicode C0 y C1, sin rechazar el resto de Unicode válido.
        if (u_isISOControl(static_cast<UChar32>(codePoint))) {
            return SecurityReason::InvalidFileName;
        }

        // Rechaza caracteres reservados, separadores de carpeta y los dos puntos de ADS.
        switch (codePoint) {
        case U'<':
        case U'>':
        case U':':
        case U'"':
        case U'/':
        case U'\\':
        case U'|':
        case U'?':
        case U'*':
            return SecurityReason::InvalidFileName;
        default:
            break;
        }
    }

    if (IsReservedFileName(fileName)) {
        return SecurityReason::InvalidFileName;
    }

    // Solo supera la validación del nombre; todavía no se han evaluado las reglas.
    return SecurityReason::Allowed;
}

// Detecta nombres reservados en una copia de la parte anterior al primer punto.
bool DownloadPolicy::IsReservedFileName(const std::string& fileName) {
    std::string baseName = fileName.substr(0, fileName.find('.'));

    // Evita ocultar un dispositivo con espacios antes de la extensión, como NUL .txt.
    // Solo prepara la comparación; el nombre original no se corrige ni se modifica.
    while (!baseName.empty() && baseName.back() == ' ') {
        baseName.pop_back();
    }

    // Los identificadores de dispositivo se comparan sin distinguir mayúsculas ASCII.
    for (char& character : baseName) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }

    if (baseName == "CON" || baseName == "PRN" || baseName == "AUX" || baseName == "NUL") {
        return true;
    }

    if (!baseName.starts_with("COM") && !baseName.starts_with("LPT")) {
        return false;
    }

    // Solo un dígito del 1 al 9 identifica los dispositivos numerados de esta lista.
    const std::string suffix = baseName.substr(3);
    if (suffix.size() == 1 && suffix[0] >= '1' && suffix[0] <= '9') {
        return true;
    }

    // Windows también reserva COM y LPT con los superíndices ¹, ² y ³, codificados en UTF-8.
    return suffix == "\xC2\xB9" || suffix == "\xC2\xB2" || suffix == "\xC2\xB3";
}

// Se invoca tras validar el nombre; un punto o espacio final ya habrá provocado el rechazo.
std::string DownloadPolicy::ExtractExtension(const std::string& fileName) {
    const std::string::size_type lastDot = fileName.find_last_of('.');

    // Un punto inicial aislado identifica nombres como .env, que se consideran sin extensión.
    if (lastDot == std::string::npos || lastDot == 0) {
        return {};
    }

    // Conserva los bytes UTF-8 y las mayúsculas para la conversión Unicode posterior.
    return fileName.substr(lastDot + 1);
}

// Convierte con el locale raíz de ICU y publica el resultado solo al completar la operación.
bool DownloadPolicy::ToLowerUtf8(const std::string& value, std::string& lowercased) {
    // La API C de ICU recibe longitudes de 32 bits; evita una conversión numérica truncada.
    const auto maxIcuLength = (std::numeric_limits<std::int32_t>::max)();
    if (value.size() > static_cast<std::size_t>(maxIcuLength)) {
        return false;
    }

    // Rechaza UTF-8 inválido antes de pedir la conversión a ICU.
    std::size_t offset = 0;
    while (offset < value.size()) {
        char32_t codePoint = 0;
        if (!ReadUtf8CodePoint(value, offset, codePoint)) {
            return false;
        }
    }

    if (value.empty()) {
        lowercased.clear();
        return true;
    }

    // La cadena vacía selecciona el locale raíz; nullptr elegiría el idioma del sistema.
    // El puntero propietario cierra el objeto mediante ICU incluso ante una salida anticipada.
    UErrorCode status = U_ZERO_ERROR;
    const std::unique_ptr<UCaseMap, decltype(&ucasemap_close)> caseMap(
        ucasemap_open("", 0, &status), &ucasemap_close);
    if (U_FAILURE(status) || !caseMap) {
        return false;
    }

    // Consulta el tamaño necesario: la conversión puede cambiar el número de bytes.
    const auto inputLength = static_cast<std::int32_t>(value.size());
    const std::int32_t requiredLength = ucasemap_utf8ToLower(
        caseMap.get(), nullptr, 0, value.data(), inputLength, &status);
    if ((status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
        || requiredLength < 0 || requiredLength == maxIcuLength) {
        return false;
    }

    // Reserva un byte adicional para el terminador y reinicia el estado tras la consulta.
    std::string result(static_cast<std::size_t>(requiredLength) + 1, '\0');
    status = U_ZERO_ERROR;
    const std::int32_t writtenLength = ucasemap_utf8ToLower(
        caseMap.get(), result.data(), requiredLength + 1,
        value.data(), inputLength, &status);
    if (U_FAILURE(status) || writtenLength != requiredLength) {
        return false;
    }

    // El terminador no forma parte del contenido lógico de std::string.
    result.resize(static_cast<std::size_t>(writtenLength));
    lowercased = std::move(result);
    return true;
}

// Lee una secuencia UTF-8 válida sin modificar las salidas si se encuentra un error.
bool DownloadPolicy::ReadUtf8CodePoint(
    const std::string& value, std::size_t& offset, char32_t& codePoint) noexcept {
    if (offset >= value.size()) {
        return false;
    }

    const unsigned char firstByte = static_cast<unsigned char>(value[offset]);
    std::size_t byteCount = 0;
    char32_t decoded = 0;
    char32_t minimumValue = 0;

    // El primer byte determina la longitud y los primeros bits del punto de código.
    if (firstByte <= 0x7F) {
        byteCount = 1;
        decoded = firstByte;
    } else if (firstByte >= 0xC2 && firstByte <= 0xDF) {
        byteCount = 2;
        decoded = static_cast<char32_t>(firstByte & 0x1F);
        minimumValue = 0x80;
    } else if (firstByte >= 0xE0 && firstByte <= 0xEF) {
        byteCount = 3;
        decoded = static_cast<char32_t>(firstByte & 0x0F);
        minimumValue = 0x800;
    } else if (firstByte >= 0xF0 && firstByte <= 0xF4) {
        byteCount = 4;
        decoded = static_cast<char32_t>(firstByte & 0x07);
        minimumValue = 0x10000;
    } else {
        return false;
    }

    // Comprueba el tamaño restante mediante una resta para evitar desbordamientos.
    if (byteCount > value.size() - offset) {
        return false;
    }

    // Cada byte de continuación debe comenzar por los bits 10 y aporta seis bits.
    for (std::size_t index = 1; index < byteCount; ++index) {
        const unsigned char nextByte = static_cast<unsigned char>(value[offset + index]);
        if ((nextByte & 0xC0) != 0x80) {
            return false;
        }
        decoded = (decoded << 6) | static_cast<char32_t>(nextByte & 0x3F);
    }

    // Rechaza codificaciones demasiado largas, sustitutos UTF-16 y valores fuera de Unicode.
    if (decoded < minimumValue || decoded > 0x10FFFF
        || (decoded >= 0xD800 && decoded <= 0xDFFF)) {
        return false;
    }

    codePoint = decoded;
    offset += byteCount;
    return true;
}

// Aplica las reglas después de comprobar la política y la validez del nombre completo.
SecurityReason DownloadPolicy::EvaluateRules(
    const DownloadControlConfig& config, const std::string& fileName, std::string& blockedExtension) {
    // Un modo desconocido nunca se interpreta como una configuración permisiva.
    if (config.Mode() != FilterMode::Denylist && config.Mode() != FilterMode::Allowlist) {
        return SecurityReason::PolicyUnavailable;
    }

    if (!config.Enabled()) {
        return SecurityReason::Allowed;
    }

    const std::string extension = ExtractExtension(fileName);
    if (extension.empty()) {
        return SecurityReason::MissingExtension;
    }

    std::string normalizedExtension;
    if (!ToLowerUtf8(extension, normalizedExtension)) {
        return SecurityReason::InvalidFileName;
    }

    bool matched = false;
    for (const std::string& rule : config.Rules()) {
        // El snapshot conserva las reglas originales; se prepara una copia para comparar.
        std::string normalizedRule;
        if (!ToLowerUtf8(rule, normalizedRule)) {
            return SecurityReason::PolicyUnavailable;
        }

        // Conserva la normalización admitida por PolicyManager y el backend.
        if (!normalizedRule.empty() && normalizedRule.front() == '.') {
            normalizedRule.erase(0, 1);
        }
        if (normalizedRule.empty()) {
            return SecurityReason::PolicyUnavailable;
        }

        if (normalizedExtension == normalizedRule) {
            matched = true;
            break;
        }
    }

    // Sin reglas, matched permanece false: DENYLIST permite y ALLOWLIST bloquea.
    const bool denylist = config.Mode() == FilterMode::Denylist;
    const bool allowed = denylist ? !matched : matched;
    if (allowed) {
        return SecurityReason::Allowed;
    }
    blockedExtension = std::move(normalizedExtension);
    return denylist ? SecurityReason::ExtensionDenied : SecurityReason::ExtensionNotAllowed;
}

}  // namespace sbp
