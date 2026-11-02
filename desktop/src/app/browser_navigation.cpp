#include "app/browser_navigation.h"

#include "browser/browser_client.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <QMetaObject>
#include <QThread>
#include <QUrl>

#include <cstdio>
#include <string>
#include <utility>

namespace sbp {

BrowserClientCallbacks BrowserNavigation::MakeCallbacks(
    BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const {
    // Preparamos el bloque con la misma sintaxis que utilizará BrowserLifecycle al completarlo.
    BrowserClientCallbacks callbacks{};
    callbacks.on_address_changed = MakeAddressHandler(window, window_ui, close_requested);
    callbacks.on_back_availability_changed = MakeBackAvailabilityHandler(window, window_ui, close_requested);
    callbacks.on_forward_availability_changed = MakeForwardAvailabilityHandler(window, window_ui, close_requested);
    return callbacks;
}

void BrowserNavigation::Connect(BrowserWindowUi& window_ui,
                                 CefRefPtr<BrowserClient> browser_client,
                                 const bool& close_requested) const {
    // El método público coordina las conexiones; cada control mantiene su implementación separada.
    ConnectBackControl(window_ui, browser_client, close_requested);
    ConnectForwardControl(window_ui, browser_client, close_requested);
    ConnectAddressControl(window_ui, browser_client, close_requested);
}

// Conecta únicamente la petición Atrás; el método es privado en BrowserNavigation.
void BrowserNavigation::ConnectBackControl(BrowserWindowUi& window_ui,
                                           const CefRefPtr<BrowserClient>& browser_client,
                                           const bool& close_requested) const {
    // Esta lambda recibe la petición Atrás en Qt y publica la operación en el hilo UI de CEF.
    window_ui.SetBackRequestHandler([browser_client, &close_requested] {
        // Una activación tardía no debe iniciar navegación durante el cierre de la ventana.
        if (close_requested) {
            return;
        }
        // BindOnce conserva el cliente hasta ejecutar su método GoBack en CEF.
        if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::GoBack, browser_client))) {
            std::fputs("Qt back navigation request could not be posted to CEF.\n", stderr);
        }
    });
}

// Conecta únicamente la petición Adelante; el método es privado en BrowserNavigation.
void BrowserNavigation::ConnectForwardControl(BrowserWindowUi& window_ui,
                                               const CefRefPtr<BrowserClient>& browser_client,
                                               const bool& close_requested) const {
    // Esta lambda recibe la petición Adelante en Qt y publica la operación en el hilo UI de CEF.
    window_ui.SetForwardRequestHandler([browser_client, &close_requested] {
        // Una activación tardía no debe iniciar navegación durante el cierre de la ventana.
        if (close_requested) {
            return;
        }
        // BindOnce conserva el cliente hasta ejecutar su método GoForward en CEF.
        if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::GoForward, browser_client))) {
            std::fputs("Qt forward navigation request could not be posted to CEF.\n", stderr);
        }
    });
}

// Conecta únicamente la barra de dirección, con su validación y envío de navegación a CEF.
void BrowserNavigation::ConnectAddressControl(BrowserWindowUi& window_ui,
                                              const CefRefPtr<BrowserClient>& browser_client,
                                              const bool& close_requested) const {
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

std::function<void(std::string)> BrowserNavigation::MakeAddressHandler(
    BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const {
    // CEF entrega una copia UTF-8; esta lambda solo publica su recepción para Qt.
    return [&window, &window_ui, &close_requested](std::string address) {
        // La lambda interior posee el texto y consulta las referencias de sesión únicamente en Qt.
        const bool queued = QMetaObject::invokeMethod(&window,
            [&window_ui, &close_requested, address = std::move(address)] {
                if (close_requested || window_ui == nullptr) {
                    return;
                }
                window_ui->SetAddress(QString::fromStdString(address));
                std::fprintf(stderr, "Qt received main address; bytes %zu; Qt main thread: %d.\n",
                             address.size(), QThread::isMainThread());
            }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF address notification could not be queued to Qt.\n", stderr);
        }
    };
}

std::function<void(bool)> BrowserNavigation::MakeBackAvailabilityHandler(
    BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const {
    // CEF entrega un valor simple; esta lambda publica la actualización para la ventana Qt.
    return [&window, &window_ui, &close_requested](bool available) {
        // Solo el hilo Qt consulta los controles y modifica la disponibilidad del botón.
        const bool queued = QMetaObject::invokeMethod(&window,
            [&window_ui, &close_requested, available] {
                if (close_requested || window_ui == nullptr) {
                    return;
                }
                window_ui->SetBackAvailable(available);
                std::fprintf(stderr, "Qt back availability updated; available: %d; Qt main thread: %d.\n",
                             available, QThread::isMainThread());
            }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF back availability could not be queued to Qt.\n", stderr);
        }
    };
}

std::function<void(bool)> BrowserNavigation::MakeForwardAvailabilityHandler(
    BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const {
    // CEF entrega la disponibilidad; esta lambda publica el aviso para el hilo principal Qt.
    return [&window, &window_ui, &close_requested](bool available) {
        // La lambda interior consulta los controles y el estado de cierre únicamente en Qt.
        const bool queued = QMetaObject::invokeMethod(&window,
            [&window_ui, &close_requested, available] {
                if (close_requested || window_ui == nullptr) {
                    return;
                }
                window_ui->SetForwardAvailable(available);
                std::fprintf(stderr, "Qt forward availability updated; available: %d; Qt main thread: %d.\n",
                             available, QThread::isMainThread());
            }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF forward availability could not be queued to Qt.\n", stderr);
        }
    };
}

}  // namespace sbp
