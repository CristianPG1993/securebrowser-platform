#include "app/browser_window_bridge.h"

#include "browser/browser_client.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <windows.h>

#include <cstdio>
#include <utility>

namespace sbp {

void BrowserWindowBridge::RequestBrowserFocus(CefRefPtr<BrowserClient> browser_client,
                                              const bool& close_requested) {
    if (close_requested) {
        return;
    }
    if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::FocusBrowser, browser_client))) {
        std::fputs("Qt focus notification could not be posted to CEF.\n", stderr);
    }
}

BrowserWindowBridge::BrowserWindowBridge(CefRefPtr<BrowserClient> browser_client,
                                         const bool& close_requested, const HWND& host_handle)
    : browser_client_(std::move(browser_client)),
      close_requested_(close_requested), host_handle_(host_handle) {}

BrowserWindowBridge::~BrowserWindowBridge() = default;

BrowserHostCallbacks BrowserWindowBridge::MakeHostCallbacks() const {
    return {
        // Qt solicita ajustar la ventana hija; CEF consultará el área nativa más reciente.
        .on_resized = [browser_client = browser_client_, &closing = close_requested_] {
            if (closing) {
                return;
            }
            if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::ResizeBrowser, browser_client))) {
                std::fputs("Qt resize notification could not be posted to CEF.\n", stderr);
            }
        },
        // El anfitrión reutiliza la misma operación que atiende el foco del marco nativo.
        .on_focus_requested = [browser_client = browser_client_, &closing = close_requested_] {
            RequestBrowserFocus(browser_client, closing);
        }
    };
}

void BrowserWindowBridge::Connect(BrowserWindow& window, BrowserWindowUi& window_ui) const {
    BrowserHost& browser_host = window_ui.GetBrowserHost();
    // Qt ejecuta esta lambda después del cambio nativo; consultamos el destino actual del teclado.
    window.SetNativeFocusHandler([&window, &window_ui, &browser_host,
                                  browser_client = browser_client_,
                                  &closing = close_requested_, &host = host_handle_] {
        const HWND native_focus = GetFocus();
        if (host != nullptr && (native_focus == host || IsChild(host, native_focus))) {
            window_ui.ClearAddressFocus();
            return;
        }
        // Respetamos el control Qt recordado antes de reenviar el foco al contenido web.
        const QWidget* remembered_focus = window.focusWidget();
        if (remembered_focus == nullptr || remembered_focus == &window
            || remembered_focus == &browser_host) {
            RequestBrowserFocus(browser_client, closing);
        }
    });
}

void BrowserWindowBridge::Disconnect(BrowserWindow& window) const {
    // La ventana deja de consultar los controles antes de que se destruyan.
    window.SetNativeFocusHandler({});
}

HWND BrowserWindowBridge::Show(BrowserWindow& window, BrowserWindowUi& window_ui) const {
    window.show();
    BrowserHost& browser_host = window_ui.GetBrowserHost();
    // El área web recibe el foco inicial; winId solo se consulta desde el hilo Qt.
    browser_host.setFocus(Qt::OtherFocusReason);
    return reinterpret_cast<HWND>(browser_host.winId());
}

}  // namespace sbp
