#pragma once

#include <QString>
#include <QUrl>

#include <optional>

namespace sbp {

// Datos de la instalación que Desktop necesita para iniciar la sesión y buscar su dispositivo.
struct BackendConfig {
    QUrl apiBaseUrl;
    QString deviceIdentifier;

    // Lee la sección [backend]. Devuelve nullopt si el archivo o alguno de sus dos valores es inválido.
    [[nodiscard]] static std::optional<BackendConfig> FromIniFile(const QString& filePath);
};

}  // namespace sbp
