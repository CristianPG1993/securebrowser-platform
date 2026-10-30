#pragma once

#include "ui/browser_host.h"

#include <QLineEdit>
#include <QVBoxLayout>

namespace sbp {

class BrowserWindow;

// Reúne la construcción y distribución de los controles de la ventana, sin depender de CEF.
// Se crea después de BrowserWindow y se destruye antes de ella, siempre en el hilo Qt.
class BrowserWindowUi final {
public:
    // Los avisos del anfitrión se reciben ya preparados por quien coordina la sesión.
    BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks);
    BrowserWindowUi(const BrowserWindowUi&) = delete;
    BrowserWindowUi& operator=(const BrowserWindowUi&) = delete;

    // La sesión necesita el anfitrión para obtener su ventana nativa y coordinar el foco.
    BrowserHost& GetBrowserHost();

private:
    // El orden permite destruir los controles antes del layout y de la ventana que los contiene.
    QVBoxLayout window_layout_;
    QLineEdit address_input_;
    BrowserHost browser_host_;
};

}  // namespace sbp
