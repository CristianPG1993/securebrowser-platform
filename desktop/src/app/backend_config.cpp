#include "app/backend_config.h"

#include "rest/auth_session.h"

#include <QFileInfo>
#include <QSettings>
#include <QUuid>

namespace sbp {

std::optional<BackendConfig> BackendConfig::FromIniFile(const QString& filePath) {
    if (!QFileInfo(filePath).isFile()) {
        return std::nullopt;
    }

    // Cada clave se lee dentro de [backend], tal como aparece en el archivo de la instalación.
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("backend"));
    const QUrl apiBaseUrl(settings.value(QStringLiteral("apiBaseUrl")).toString(), QUrl::StrictMode);
    const QString deviceIdentifier = settings.value(QStringLiteral("deviceIdentifier")).toString();

    if (settings.status() != QSettings::NoError || !AuthSession::IsValidApiBaseUrl(apiBaseUrl)) {
        return std::nullopt;
    }
    // FetchDeviceIdentity compara el UUID en minúsculas, con guiones y sin llaves.
    if (QUuid::fromString(deviceIdentifier).toString(QUuid::WithoutBraces) != deviceIdentifier) {
        return std::nullopt;
    }
    return BackendConfig{apiBaseUrl, deviceIdentifier};
}

}  // namespace sbp
