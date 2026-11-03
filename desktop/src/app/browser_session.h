#pragma once

#include "app/browser_lifecycle.h"

class QApplication;

namespace sbp {

class BrowserWindowUi;

// Coordina la ventana Qt y su navegador CEF; el arranque de Windows queda fuera.
// Todos sus métodos y su estado pertenecen al hilo principal de Qt.
class BrowserSession final {
public:
    BrowserSession() = default;
    BrowserSession(const BrowserSession&) = delete;
    BrowserSession& operator=(const BrowserSession&) = delete;

    // Recibe en Qt el aviso que BrowserApp ha enviado desde el hilo de CEF.
    void OnContextInitialized();

    // Crea la interfaz, conecta los avisos y ejecuta el bucle Qt una sola vez.
    // Devuelve el código de salida después de destruir los widgets de esta sesión.
    int Run(QApplication& qt_app);

private:
    // Vive hasta después de CefShutdown; los callbacks consultan su estado desde Qt.
    BrowserLifecycle lifecycle_;
    // Referencia no propietaria a los controles; solo se consulta en Qt y se retira antes de destruirlos.
    BrowserWindowUi* window_ui_ = nullptr;

};

}  // namespace sbp
