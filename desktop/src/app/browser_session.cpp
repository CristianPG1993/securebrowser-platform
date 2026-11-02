#include "app/browser_session.h"

#include "browser/browser_client.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <QApplication>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <QWidget>

#include <cstdio>
#include <string>
#include <utility>

namespace sbp {

void BrowserSession::OnContextInitialized() {
    std::fprintf(stderr, "CEF context notification received; Qt main thread: %d.\n",
                 QThread::isMainThread());
    cef_context_ready_ = true;
    PrepareBrowserWindowInfo();
}

// Puede llamarse al recibir el contexto CEF o al crear el anfitrión, en cualquier orden.
void BrowserSession::PrepareBrowserWindowInfo() {
    if (!cef_context_ready_ || browser_host_handle_ == nullptr || browser_window_info_ready_
        || qt_close_requested_) {
        return;
    }

    // Medimos el área nativa en píxeles, desde el hilo Qt, para respetar el escalado de Windows.
    RECT host_bounds{};
    if (!GetClientRect(browser_host_handle_, &host_bounds)
        || host_bounds.right <= 0 || host_bounds.bottom <= 0) {
        std::fputs("CEF window preparation failed: native host area unavailable.\n", stderr);
        return;
    }

    // SetAsChild solo rellena los parámetros: todavía no crea ninguna ventana ni navegador CEF.
    browser_window_info_.SetAsChild(browser_host_handle_,
        CefRect(0, 0, static_cast<int>(host_bounds.right), static_cast<int>(host_bounds.bottom)));
    browser_window_info_.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    browser_window_info_.windowless_rendering_enabled = false;
    // Impide preparar dos veces la misma solicitud cuando ambas condiciones ya se cumplen.
    browser_window_info_ready_ = true;
    std::fprintf(stderr,
                 "CEF child window parameters prepared: parent %p; size %dx%d; runtime style %d; Qt main thread: %d.\n",
                 static_cast<void*>(browser_window_info_.parent_window),
                 browser_window_info_.bounds.width, browser_window_info_.bounds.height,
                 static_cast<int>(browser_window_info_.runtime_style), QThread::isMainThread());
    // Solo ahora están preparados tanto CEF como el anfitrión nativo de Qt.
    request_browser_creation_(browser_window_info_);
}

int BrowserSession::Run(QApplication& qt_app) {
    int qt_exit_code = 0;
    // Los widgets se destruyen al salir de este ámbito, antes de volver al arranque.
    {
        // La ventana principal contendrá un widget hijo reservado al contenido web.
        sbp::BrowserWindow window;
        // Esta función puede recibir el aviso de cierre CEF y trasladarlo a la propia ventana Qt.
        // La ventana debe seguir viva hasta recibir la confirmación; su closeEvent la conserva.
        const auto confirm_close_on_qt = [this, &window] {
            // Esta lambda interior confirma el cierre al procesarse en el hilo Qt.
            const bool queued = QMetaObject::invokeMethod(&window, [this, &window] {
                // El receptor es window, que sigue vivo; no se muestra ningún diálogo al usuario.
                std::fprintf(stderr, "Qt close confirmed; native host valid: %d.\n",
                             IsWindow(browser_host_handle_) != FALSE);
                window.ConfirmClose();
            }, Qt::QueuedConnection);
            if (!queued) {
                std::fputs("Qt close confirmation could not be queued.\n", stderr);
            }
        };

        // El cliente no conserva CefBrowser: sus funciones notifican a Qt sin transferir esa referencia.
        CefRefPtr<sbp::BrowserClient> browser_client(new sbp::BrowserClient(sbp::BrowserClientCallbacks{
            // Este callback recibe el identificador en CEF y publica su aviso para Qt.
            .on_browser_created = [&window](int browser_id) {
                // CEF invoca esta función en su hilo UI; el identificador se copia a la cola Qt.
                // La lambda interior solo copia el identificador, sin conservar el navegador.
                const bool queued = QMetaObject::invokeMethod(&window, [browser_id] {
                    std::fprintf(stderr, "Qt received browser creation: id %d; Qt main thread: %d.\n",
                                 browser_id, QThread::isMainThread());
                }, Qt::QueuedConnection);
                if (!queued) {
                    std::fputs("CEF browser creation notification could not be queued to Qt.\n", stderr);
                }
            },
            .on_browser_closed = confirm_close_on_qt,
            // CEF entrega una copia de la dirección; esta lambda publica su recepción para Qt.
            .on_address_changed = [this, &window](std::string address) {
                // La lambda interior posee la cadena y se ejecutará en el hilo Qt de la ventana.
                const bool queued = QMetaObject::invokeMethod(&window,
                    [this, address = std::move(address)] {
                        if (qt_close_requested_) {
                            return;
                        }
                        // Comprobamos la recepción del texto en el hilo principal de Qt.
                        std::fprintf(stderr, "Qt received main address; bytes %zu; Qt main thread: %d.\n",
                                     address.size(), QThread::isMainThread());
                    }, Qt::QueuedConnection);
                if (!queued) {
                    std::fputs("CEF address notification could not be queued to Qt.\n", stderr);
                }
            }
        }));
        std::fputs("CEF browser client prepared.\n", stderr);

        // BindOnce copia los parámetros y conserva el cliente hasta ejecutar la tarea en CEF.
        request_browser_creation_ = [this, browser_client, &window,
                                     &qt_exit_code](const CefWindowInfo& window_info) {
            if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::CreateBrowser,
                                                   browser_client, window_info))) {
                std::fputs("CEF browser creation task could not be posted.\n", stderr);
                // No hay una tarea ni una creación pendiente: Qt puede terminar con error.
                qt_exit_code = 1;
                qt_close_requested_ = true;
                window.ConfirmClose();
            }
        };

        // Esta lambda convierte la solicitud de cierre Qt en una tarea para el cliente CEF.
        window.SetCloseRequestHandler([this, browser_client] {
            // Impide iniciar una creación posterior si el aviso de contexto aún estaba en la cola Qt.
            qt_close_requested_ = true;
            // Qt publica una tarea en TID_UI, el hilo de interfaz de CEF, sin esperar su ejecución.
            // La captura y BindOnce conservan referencias administradas al cliente, sin copiarlo.
            // &BrowserClient::RequestClose identifica el método que ejecutará la tarea sobre ese cliente.
            const bool posted = CefPostTask(TID_UI,
                base::BindOnce(&sbp::BrowserClient::RequestClose, browser_client));
            if (!posted) {
                std::fputs("Qt close request could not be posted to CEF.\n", stderr);
            }
        });

        // Esta lambda comparte la solicitud de foco entre el anfitrión Qt y el marco nativo.
        const auto request_browser_focus = [this, browser_client] {
            if (qt_close_requested_) {
                return;
            }
            if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::FocusBrowser,
                                                   browser_client))) {
                std::fputs("Qt focus notification could not be posted to CEF.\n", stderr);
            }
        };

        // La clase de interfaz crea y distribuye todos los controles; la sesión aporta sus avisos.
        // Cada callback conserva una referencia al cliente y conecta el evento Qt con una tarea CEF.
        sbp::BrowserWindowUi window_ui(window, {
            // Qt ejecuta esta lambda al redimensionar el anfitrión; solicita ajustar la ventana hija CEF.
            .on_resized = [this, browser_client] {
                if (qt_close_requested_) {
                    return;
                }
                // La tarea consulta el área nativa más reciente al ejecutarse en CEF, sin bloquear Qt.
                if (!CefPostTask(TID_UI, base::BindOnce(&sbp::BrowserClient::ResizeBrowser,
                                                       browser_client))) {
                    std::fputs("Qt resize notification could not be posted to CEF.\n", stderr);
                }
            },
            // El aviso Qt reutiliza la misma función que atiende la notificación del marco nativo.
            .on_focus_requested = request_browser_focus
        });
        // Esta lambda recibe el texto al pulsar Enter, directamente en el mismo hilo Qt.
        window_ui.SetAddressSubmitHandler([this, browser_client](const QString& address) {
            if (qt_close_requested_) {
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
        // Usamos el anfitrión creado por la interfaz, sin construir otro ni copiarlo.
        sbp::BrowserHost& browser_host = window_ui.GetBrowserHost();
        // Esta lambda sincroniza el foco lógico de Qt después de un cambio de foco nativo.
        window.SetNativeFocusHandler([this, &window, &window_ui, &browser_host, request_browser_focus] {
            // Consultamos el destino actual, porque el usuario pudo volver a la barra mientras se encolaba el aviso.
            const HWND native_focus = GetFocus();
            if (browser_host_handle_ != nullptr
                && (native_focus == browser_host_handle_ || IsChild(browser_host_handle_, native_focus))) {
                window_ui.ClearAddressFocus();
                return;
            }
            // Al recuperar el marco, respetamos los controles Qt recordados antes de reenviar foco a CEF.
            const QWidget* remembered_focus = window.focusWidget();
            if (remembered_focus == nullptr || remembered_focus == &window
                || remembered_focus == &browser_host) {
                request_browser_focus();
            }
        });
        window.show();
        // El área web será el primer destino del teclado cuando esta ventana esté activa.
        browser_host.setFocus(Qt::OtherFocusReason);

        // winId() se consulta en el hilo Qt; en Windows identifica la ventana mediante un HWND.
        // Conservamos el identificador para preparar los parámetros del navegador hijo.
        browser_host_handle_ = reinterpret_cast<HWND>(browser_host.winId());
        std::fprintf(stderr, "Qt native browser host ready: %p.\n",
                     static_cast<void*>(browser_host_handle_));
        // Si el aviso CEF ya se procesó, esta llamada completa la preparación; si no, lo hará el aviso.
        PrepareBrowserWindowInfo();

        // Qt espera y despacha eventos en el principal; CEF mantiene su bucle en su propio hilo.
        // Al cerrar la última ventana, exec() termina y conserva el código de salida de Qt.
        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);
        const int event_loop_exit_code = qt_app.exec();
        if (qt_exit_code == 0) {
            qt_exit_code = event_loop_exit_code;
        }

        // Retiramos el identificador y los parámetros antes de destruir los widgets anfitriones.
        // Soltamos también la función y su referencia al cliente antes de apagar CEF.
        request_browser_creation_ = {};
        window.SetNativeFocusHandler({});
        browser_host_handle_ = nullptr;
        browser_window_info_ = CefWindowInfo();
        browser_window_info_ready_ = false;
    }
    // Al salir del bloque se destruye la ventana, antes de apagar el motor.
    // OnBeforeClose ya ha confirmado el cierre del navegador antes de destruir su anfitrión.
    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    return qt_exit_code;
}

}  // namespace sbp
