#include "ui/browser_window_ui.h"

#include "ui/browser_window.h"

#include <utility>

namespace sbp {

BrowserWindowUi::BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks)
    : window_layout_(&window),
      address_input_(&window),
      browser_host_(std::move(host_callbacks), &window) {
    // Aquí se configura el aspecto inicial de la ventana y la distribución de sus controles.
    window.setWindowTitle("SecureBrowser");
    window.resize(1024, 768);
    window_layout_.setContentsMargins(0, 0, 0, 0);

    // El campo permite editar una dirección; todavía no solicita navegación al pulsar Enter.
    address_input_.setObjectName("address_input");
    address_input_.setAccessibleName("Dirección web");
    address_input_.setPlaceholderText("Introduce una URL HTTP o HTTPS");

    // El orden del layout coloca la dirección arriba y reserva el espacio restante al contenido web.
    window_layout_.addWidget(&address_input_);
    window_layout_.addWidget(&browser_host_);
}

BrowserHost& BrowserWindowUi::GetBrowserHost() {
    // Devuelve el control existente por referencia; no construye ni copia otro anfitrión.
    return browser_host_;
}

}  // namespace sbp
