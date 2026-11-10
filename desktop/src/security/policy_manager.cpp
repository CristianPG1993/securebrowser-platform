#include "security/policy_manager.h"

#include <unicode/ucasemap.h>
#include <unicode/uchar.h>

#include <limits>
#include <memory>
#include <regex>
#include <string>
#include <unordered_set>
#include <utility>

namespace sbp {

// Detiene la validación en el primer error, sin publicar ni modificar la política recibida.
PolicyValidationError PolicyManager::Validate(
    const PolicySnapshot& policy, std::int64_t expectedCompanyId) const {
    PolicyValidationError error = ValidateIdentity(policy, expectedCompanyId);
    if (error != PolicyValidationError::None) {
        return error;
    }

    error = ValidateName(policy.Name());
    if (error != PolicyValidationError::None) {
        return error;
    }

    error = ValidateModes(policy);
    if (error != PolicyValidationError::None) {
        return error;
    }

    error = ValidateUrlRules(policy.UrlFiltering());
    if (error != PolicyValidationError::None) {
        return error;
    }

    return ValidateDownloadRules(policy.DownloadControl());
}

// Rechaza identidades inválidas o ajenas sin modificar ni publicar la política.
PolicyValidationError PolicyManager::ValidateIdentity(
    const PolicySnapshot& policy, std::int64_t expectedCompanyId) noexcept {
    if (policy.Id() <= 0) {
        return PolicyValidationError::InvalidPolicyId;
    }

    if (policy.CompanyId() <= 0) {
        return PolicyValidationError::InvalidCompanyId;
    }

    if (policy.CompanyId() != expectedCompanyId) {
        return PolicyValidationError::CompanyMismatch;
    }

    return PolicyValidationError::None;
}

// Comprueba UTF-8 y el límite tras ignorar los espacios de los extremos como String.strip().
PolicyValidationError PolicyManager::ValidateName(const std::string& name) {
    // Posición en bytes desde la que se leerá el siguiente punto de código UTF-8.
    std::size_t offset = 0;

    // Total de puntos de código leídos, incluidos los espacios iniciales, interiores y finales.
    std::size_t codePointCount = 0;

    // Cantidad de puntos de código de espacio anteriores al primer carácter de contenido.
    std::size_t leadingWhitespaceCount = 0;

    // Longitud en puntos de código entre el primer y el último carácter de contenido leídos.
    // Incluye espacios interiores y excluye los de los extremos.
    std::size_t strippedLength = 0;

    while (offset < name.size()) {
        char32_t codePoint = 0;
        if (!ReadUtf8CodePoint(name, offset, codePoint)) {
            return PolicyValidationError::InvalidName;
        }
        ++codePointCount;

        // Equivale a Character.isWhitespace de Java 25 (Unicode 16.0).
        // Los espacios inseparables U+00A0, U+2007 y U+202F no se eliminan en Java.
        const bool isWhitespace =
            (codePoint >= 0x09 && codePoint <= 0x0D)
            || (codePoint >= 0x1C && codePoint <= 0x20)
            || codePoint == 0x1680
            || (codePoint >= 0x2000 && codePoint <= 0x2006)
            || (codePoint >= 0x2008 && codePoint <= 0x200A)
            || codePoint == 0x2028 || codePoint == 0x2029
            || codePoint == 0x205F || codePoint == 0x3000;

        if (isWhitespace) {
            if (strippedLength == 0) {
                ++leadingWhitespaceCount;
            }
        } else {
            // Actualiza hasta el último carácter de contenido: cuenta espacios interiores,
            // pero deja fuera los iniciales y los que finalmente queden al final.
            strippedLength = codePointCount - leadingWhitespaceCount;
            if (strippedLength > 150) {
                return PolicyValidationError::InvalidName;
            }
        }
    }

    if (strippedLength == 0) {
        return PolicyValidationError::InvalidName;
    }

    return PolicyValidationError::None;
}

// Exige modos conocidos aunque las funcionalidades no estén activadas.
PolicyValidationError PolicyManager::ValidateModes(const PolicySnapshot& policy) noexcept {
    const FilterMode urlMode = policy.UrlFiltering().Mode();
    if (urlMode != FilterMode::Denylist && urlMode != FilterMode::Allowlist) {
        return PolicyValidationError::InvalidUrlFilteringMode;
    }

    const FilterMode downloadMode = policy.DownloadControl().Mode();
    if (downloadMode != FilterMode::Denylist && downloadMode != FilterMode::Allowlist) {
        return PolicyValidationError::InvalidDownloadControlMode;
    }

    return PolicyValidationError::None;
}

// Valida todas las reglas ASCII del backend, sin modificar las listas del snapshot.
PolicyValidationError PolicyManager::ValidateUrlRules(const UrlFilteringConfig& config) {
    // Cada etiqueta tiene entre 1 y 63 caracteres, con guiones solo en el interior.
    static const std::regex domainPattern(
        "[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?"
        "(?:[.][a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?)*");
    std::unordered_set<std::string> normalizedRules;

    for (const std::string& rule : config.Rules()) {
        // El límite admite un único punto final que se retira antes de comparar.
        if (rule.empty() || rule.size() > 254) {
            return PolicyValidationError::InvalidUrlRule;
        }

        std::string normalized = rule;
        for (char& character : normalized) {
            if (character >= 'A' && character <= 'Z') {
                character = static_cast<char>(character - 'A' + 'a');
            }
        }
        if (normalized.back() == '.') {
            normalized.pop_back();
        }

        // Los dominios internacionales deben llegar convertidos a ASCII por el backend.
        if (normalized.empty() || normalized.size() > 253
            || !std::regex_match(normalized, domainPattern)) {
            return PolicyValidationError::InvalidUrlRule;
        }

        if (!normalizedRules.insert(normalized).second) {
            return PolicyValidationError::DuplicateUrlRule;
        }
    }

    return PolicyValidationError::None;
}

// Comprueba todas las extensiones, incluso con el control desactivado, sin modificar el snapshot.
PolicyValidationError PolicyManager::ValidateDownloadRules(const DownloadControlConfig& config) {
    std::unordered_set<std::string> normalizedRules;

    for (const std::string& rule : config.Rules()) {
        std::string normalized;
        if (!ToLowerUtf8(rule, normalized)) {
            return PolicyValidationError::InvalidDownloadRule;
        }

        // El backend permite un punto inicial opcional, que no forma parte de la extensión.
        if (!normalized.empty() && normalized.front() == '.') {
            normalized.erase(0, 1);
        }
        if (normalized.empty()) {
            return PolicyValidationError::InvalidDownloadRule;
        }

        // Posición en bytes de la siguiente lectura y cantidad de puntos de código normalizados.
        std::size_t offset = 0;
        std::size_t codePointCount = 0;
        while (offset < normalized.size()) {
            char32_t codePoint = 0;
            if (!ReadUtf8CodePoint(normalized, offset, codePoint)) {
                return PolicyValidationError::InvalidDownloadRule;
            }
            ++codePointCount;

            // Estas funciones de ICU corresponden a las comprobaciones Character del backend.
            const auto character = static_cast<UChar32>(codePoint);
            const bool isInvalidCharacter = u_isWhitespace(character)
                || u_isJavaSpaceChar(character) || u_isISOControl(character)
                || character == '.' || character == '/' || character == '\\' || character == ':';
            if (codePointCount > 20 || isInvalidCharacter) {
                return PolicyValidationError::InvalidDownloadRule;
            }
        }

        if (!normalizedRules.insert(normalized).second) {
            return PolicyValidationError::DuplicateDownloadRule;
        }
    }

    return PolicyValidationError::None;
}

// Convierte con el locale raíz de ICU y publica el resultado solo al completar la operación.
bool PolicyManager::ToLowerUtf8(const std::string& value, std::string& lowercased) {
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

// Lee una secuencia UTF-8 conforme a RFC 3629 sin consumir datos si es inválida.
bool PolicyManager::ReadUtf8CodePoint(
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

}  // namespace sbp
