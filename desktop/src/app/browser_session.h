#pragma once

#include "app/browser_lifecycle.h"
#include "app/startup_options.h"

class QApplication;

namespace sbp {

class BrowserWindowUi;
struct NavigationDecisionNotice;

// Coordina la ventana Qt y su navegador CEF; el arranque de Windows queda fuera.
// La coordinación y su estado pertenecen al hilo principal de Qt.
// El receptor estático OnNavigationDecision se ejecuta en el hilo UI de CEF.
class BrowserSession final {
public:
    // Conserva una copia de las opciones validadas; recibirlas no carga ninguna política.
    explicit BrowserSession(const StartupOptions& startup_options);
    BrowserSession(const BrowserSession&) = delete;
    BrowserSession& operator=(const BrowserSession&) = delete;

    // Recibe en Qt el aviso que BrowserApp ha enviado desde el hilo de CEF.
    void OnContextInitialized();

    // Crea la interfaz, conecta los avisos y ejecuta el bucle Qt una sola vez.
    // Devuelve el código de salida después de destruir los widgets de esta sesión.
    int Run(QApplication& qt_app);

private:
    // Receptor provisional observable; UI y productor de eventos se conectarán en sus tareas propias.
    static bool OnNavigationDecision(const NavigationDecisionNotice& notice);

    // La composición utilizará estas opciones; el modo queda fijado durante toda la sesión.
    const StartupOptions startup_options_;

    // Vive hasta después de CefShutdown; los callbacks consultan su estado desde Qt.
    BrowserLifecycle lifecycle_;
    // Referencia no propietaria a los controles; solo se consulta en Qt y se retira antes de destruirlos.
    BrowserWindowUi* window_ui_ = nullptr;

};

}  // namespace sbp
