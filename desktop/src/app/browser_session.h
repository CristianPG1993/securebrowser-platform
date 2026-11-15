#pragma once

#include "app/browser_lifecycle.h"
#include "app/startup_options.h"

class QApplication;

namespace sbp {

class BrowserWindowUi;
class PolicyManager;
struct NavigationDecisionNotice;

// Compone la ventana, sus controles, el gestor de políticas y los adaptadores Qt/CEF.
// Conecta esos objetos durante una sesión; el arranque de Windows queda en app_entry_win.cpp.
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
    // Prepara el gestor que utilizará el handler: en modo demo crea contexto y publica el snapshot.
    // true indica que la preparación permite continuar el arranque; el permiso de navegar lo decide el motor.
    [[nodiscard]] bool PrepareDemoPolicy(PolicyManager& manager) const;

    // Consulta el gestor por referencia de lectura y entrega el nombre publicado a los controles Qt.
    // Solo configura la presentación; leer el estado no renueva su vigencia.
    void ConfigureStatus(BrowserWindowUi& window_ui, const PolicyManager& manager) const;

    // Receptor provisional observable; UI y productor de eventos se conectarán en sus tareas propias.
    static bool OnNavigationDecision(const NavigationDecisionNotice& notice);

    // Copia de los argumentos validados: modo de ejecución y selección inicial de política.
    // La política publicada se consulta en PolicyManager; el modo queda fijado durante toda la sesión.
    const StartupOptions startup_options_;

    // Coordina la preparación de CEF y del HWND anfitrión, la creación del navegador y su cierre.
    // Vive hasta después de CefShutdown; los callbacks consultan su estado desde Qt.
    BrowserLifecycle lifecycle_;
    // Apunta al objeto window_ui local de Run para que los callbacks Qt encuentren los controles.
    // No es propietario de ese objeto y se pone a nullptr antes de destruirlo.
    BrowserWindowUi* window_ui_ = nullptr;

};

}  // namespace sbp
