#include "app/browser_session.h"

#include "app/browser_navigation.h"
#include "app/browser_window_bridge.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include <QApplication>

#include <cstdio>

namespace sbp {

void BrowserSession::OnContextInitialized() {
    lifecycle_.OnContextInitialized();
}

int BrowserSession::Run(QApplication& qt_app) {
    int qt_exit_code = 0;
    // Este ámbito posee los widgets; todos se destruyen antes de regresar al arranque CEF.
    {
        BrowserWindow window;
        // Cada módulo prepara sus avisos; la sesión reúne las dependencias y conserva su vida.
        CefRefPtr<BrowserClient> browser_client(new BrowserClient(lifecycle_.MakeClientCallbacks(
            window, MakeBrowserAddressHandler(window, window_ui_, lifecycle_.CloseRequested()))));
        std::fputs("CEF browser client prepared.\n", stderr);
        lifecycle_.Connect(window, browser_client);

        BrowserWindowBridge window_bridge(browser_client, lifecycle_.CloseRequested(), lifecycle_.HostHandle());
        BrowserWindowUi window_ui(window, window_bridge.MakeHostCallbacks());
        window_ui_ = &window_ui;
        ConnectBrowserNavigation(window_ui, browser_client, lifecycle_.CloseRequested());
        window_bridge.Connect(window, window_ui);
        lifecycle_.OnHostReady(window_bridge.Show(window, window_ui));

        // Qt atiende la sesión en el principal, mientras CEF mantiene su propio hilo UI.
        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);
        qt_exit_code = lifecycle_.ExitCode(qt_app.exec());

        // Se retiran las conexiones antes de destruir los controles que utilizan sus callbacks.
        lifecycle_.Detach();
        window_ui_ = nullptr;
        window_bridge.Disconnect(window);
    }
    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    return qt_exit_code;
}

}  // namespace sbp
