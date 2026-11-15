#pragma once

#include "app/browser_lifecycle.h"
#include "app/startup_options.h"

#include <cstdint>
#include <functional>
#include <memory>

class QApplication;
class QString;

namespace sbp {

class BrowserWindow;
class BrowserWindowUi;
class PolicyManager;
class SecurityContext;
struct NavigationDecisionNotice;

// Compone la ventana, sus controles, el gestor de políticas y los adaptadores Qt/CEF.
// Conecta esos objetos durante una sesión; el arranque de Windows queda en app_entry_win.cpp.
// La coordinación y su estado pertenecen al hilo principal de Qt.
// El receptor de CEF encola una copia; OnNavigationDecision se ejecuta únicamente en Qt.
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

    // Ofrece los presets únicamente en demo y vincula sus solicitudes al contexto inicial.
    // La función guardada en los controles conserva el gestor, pero no prolonga la vida de la UI.
    void ConfigurePolicySelector(
        BrowserWindowUi& window_ui, const std::shared_ptr<PolicyManager>& manager) const;

    // Publica en el contexto original y solo confirma el cambio visual después del éxito.
    // La política demo no caduca; el cambio no inicia otro contexto ni navega automáticamente.
    void ApplyPolicySelection(BrowserWindowUi& window_ui, PolicyManager& manager,
                              const SecurityContext& context, int index) const;

    // Prepara el receptor ejecutado en CEF; no lee controles ni estado Qt desde ese hilo.
    std::function<bool(const NavigationDecisionNotice&)> MakeNavigationDecisionHandler(
        BrowserWindow& window, const std::shared_ptr<PolicyManager>& manager);

    // Descarta avisos obsoletos y presenta el bloqueo en Qt, sin reevaluar permisos de navegación.
    void OnNavigationDecision(const NavigationDecisionNotice& notice, const PolicyManager& manager);

    // Traduce motivos controlados a texto de presentación, sin registrar URL ni datos de navegación.
    static QString NavigationBlockMessage(const NavigationDecisionNotice& notice, const QString& policy_name);

    // Copia de los argumentos validados: modo de ejecución y selección inicial de política.
    // La política publicada se consulta en PolicyManager; el modo queda fijado durante toda la sesión.
    const StartupOptions startup_options_;

    // Coordina la preparación de CEF y del HWND anfitrión, la creación del navegador y su cierre.
    // Vive hasta después de CefShutdown; los callbacks consultan su estado desde Qt.
    BrowserLifecycle lifecycle_;
    // Apunta al objeto window_ui local de Run para que los callbacks Qt encuentren los controles.
    // No es propietario de ese objeto y se pone a nullptr antes de destruirlo.
    BrowserWindowUi* window_ui_ = nullptr;

    // Solo Qt lo modifica; impide reemplazar un aviso reciente por otro anterior o duplicado.
    std::uint64_t last_notice_operation_id_ = 0;

};

}  // namespace sbp
