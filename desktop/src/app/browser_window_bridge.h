#pragma once

#include "include/cef_base.h"
#include "ui/browser_host.h"

namespace sbp {

class BrowserClient;
class BrowserWindow;
class BrowserWindowUi;

// Conecta únicamente tamaño y foco entre las ventanas Qt y CEF, desde el hilo Qt.
// Las referencias de estado pertenecen a la sesión y viven más que los controles.
class BrowserWindowBridge final {
public:
    BrowserWindowBridge(CefRefPtr<BrowserClient> browser_client,
                        const bool& close_requested, const HWND& host_handle);

    ~BrowserWindowBridge();

    // Prepara los avisos que recibe BrowserHost durante su construcción.
    BrowserHostCallbacks MakeHostCallbacks() const;

    // Instala y retira la sincronización del foco nativo del marco.
    void Connect(BrowserWindow& window, BrowserWindowUi& window_ui) const;
    void Disconnect(BrowserWindow& window) const;

    // Muestra la ventana, prepara el foco inicial y devuelve el identificador nativo del anfitrión.
    HWND Show(BrowserWindow& window, BrowserWindowUi& window_ui) const;

private:
    // El cliente se conserva mediante una referencia administrada; el estado solo se consulta en Qt.
    const CefRefPtr<BrowserClient> browser_client_;
    const bool& close_requested_;
    const HWND& host_handle_;
};

}  // namespace sbp
