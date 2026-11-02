#include "app/browser_navigation.h"

#include "browser/browser_client.h"
#include "ui/browser_window_ui.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <QThread>
#include <QUrl>

#include <cstdio>
#include <string>

namespace sbp {

void ConnectBrowserNavigation(BrowserWindowUi& window_ui,
                              CefRefPtr<BrowserClient> browser_client,
                              const bool& close_requested) {
    // Esta lambda recibe el texto al pulsar Enter, directamente en el mismo hilo Qt.
    window_ui.SetAddressSubmitHandler([browser_client, &close_requested](const QString& address) {
        if (close_requested) {
            return;
        }
        // Comprobamos la recepción sin registrar la dirección introducida por el usuario.
        std::fprintf(stderr, "Qt address submitted; length %lld; Qt main thread: %d.\n",
                     static_cast<long long>(address.size()), QThread::isMainThread());

        // Quitamos espacios exteriores y analizamos la dirección en modo estricto.
        const QString address_text = address.trimmed();
        QUrl url(address_text, QUrl::StrictMode);
        // Si el usuario escribe un servidor sin esquema, usamos HTTPS por defecto: google.com -> https://google.com.
        if (!address_text.isEmpty() && url.scheme().isEmpty()) {
            url = QUrl(QStringLiteral("https://") + address_text, QUrl::StrictMode);
        }
        // Validamos el resultado completo, tanto si el esquema venía escrito como si lo añadimos.
        const QString scheme = url.scheme();
        if (!url.isValid() || url.host().isEmpty()
            || (scheme != QStringLiteral("http") && scheme != QStringLiteral("https"))) {
            std::fputs("Qt address rejected: expected an absolute HTTP or HTTPS URL with a host.\n",
                       stderr);
            return;
        }
        // Serializamos la misma URL que acabamos de validar, conservando su codificación.
        const std::string target_url = url.toEncoded().toStdString();
        // BindOnce conserva el cliente y una copia de la dirección hasta ejecutar Navigate en CEF.
        if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::Navigate,
                                               browser_client, target_url))) {
            std::fputs("Qt navigation request could not be posted to CEF.\n", stderr);
        }
    });
}

}  // namespace sbp
