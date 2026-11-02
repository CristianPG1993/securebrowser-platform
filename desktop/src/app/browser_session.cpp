#include "app/browser_session.h"

#include "app/browser_navigation.h"
#include "app/browser_window_bridge.h"

#include "browser/browser_client.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include <QApplication>
#include <QMetaObject>
#include <QThread>

#include <cstdio>
#include <string>
#include <utility>

namespace sbp {

void BrowserSession::OnContextInitialized() {
    lifecycle_.OnContextInitialized();
}

int BrowserSession::Run(QApplication& qt_app) {
    int qt_exit_code = 0;
    // Los widgets se destruyen al salir de este ámbito, antes de volver al arranque.
    {
        // La ventana principal contendrá un widget hijo reservado al contenido web.
        sbp::BrowserWindow window;
        // El ciclo de vida aporta los avisos de creación y cierre; la dirección conserva su puente Qt.
        CefRefPtr<BrowserClient> browser_client(new BrowserClient(lifecycle_.MakeClientCallbacks(window,
            // CEF entrega una copia de la dirección; esta lambda publica su recepción para Qt.
            [this, &window](std::string address) {
                // La lambda interior posee la cadena y se ejecutará en el hilo Qt de la ventana.
                const bool queued = QMetaObject::invokeMethod(&window,
                    [this, address = std::move(address)] {
                        if (lifecycle_.CloseRequested() || window_ui_ == nullptr) {
                            return;
                        }
                        // Ya estamos en Qt: convertimos la copia UTF-8 y actualizamos el control visual.
                        window_ui_->SetAddress(QString::fromStdString(address));
                        std::fprintf(stderr, "Qt received main address; bytes %zu; Qt main thread: %d.\n",
                                     address.size(), QThread::isMainThread());
                    }, Qt::QueuedConnection);
                if (!queued) {
                    std::fputs("CEF address notification could not be queued to Qt.\n", stderr);
                }
            })));
        std::fputs("CEF browser client prepared.\n", stderr);
        lifecycle_.Connect(window, browser_client);

        // El puente reúne los avisos de tamaño y foco, conservando el estado en la sesión.
        BrowserWindowBridge window_bridge(browser_client, lifecycle_.CloseRequested(), lifecycle_.HostHandle());
        sbp::BrowserWindowUi window_ui(window, window_bridge.MakeHostCallbacks());
        // Los controles ya existen; el callback encolado podrá acceder a ellos desde el hilo Qt.
        window_ui_ = &window_ui;
        // El módulo de navegación instala sus callbacks; la sesión coordina su vida y el cierre.
        ConnectBrowserNavigation(window_ui, browser_client, lifecycle_.CloseRequested());
        // Usamos el anfitrión creado por la interfaz, sin construir otro ni copiarlo.
        sbp::BrowserHost& browser_host = window_ui.GetBrowserHost();
        window_bridge.Connect(window, window_ui);
        window.show();
        // El área web será el primer destino del teclado cuando esta ventana esté activa.
        browser_host.setFocus(Qt::OtherFocusReason);

        // El ciclo de vida prepara la creación con el identificador nativo del anfitrión.
        lifecycle_.OnHostReady(reinterpret_cast<HWND>(browser_host.winId()));

        // Qt espera y despacha eventos en el principal; CEF mantiene su bucle en su propio hilo.
        // Al cerrar la última ventana, exec() termina y conserva el código de salida de Qt.
        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);
        qt_exit_code = lifecycle_.ExitCode(qt_app.exec());

        // Retiramos las conexiones mientras los widgets siguen vivos.
        lifecycle_.Detach();
        window_ui_ = nullptr;
        window_bridge.Disconnect(window);
    }
    // Al salir del bloque se destruye la ventana, antes de apagar el motor.
    // OnBeforeClose ya ha confirmado el cierre del navegador antes de destruir su anfitrión.
    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    return qt_exit_code;
}

}  // namespace sbp
