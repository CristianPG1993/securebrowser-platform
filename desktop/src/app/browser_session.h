#pragma once

#include "app/browser_lifecycle.h"
#include "app/startup_options.h"
#include "browser/security_download_handler.h"
#include "browser/security_request_handler.h"

#include <cstdint>
#include <functional>
#include <memory>

class QApplication;
class QString;

namespace sbp {

class AuthSession;
class BrowserWindow;
class BrowserWindowUi;
class PolicyManager;
class SecurityContext;
struct NavigationDecisionNotice;

// Compone la ventana, sus controles, el gestor de políticas y los adaptadores Qt/CEF.
// Conecta esos objetos durante una sesión; el arranque de Windows queda en app_entry_win.cpp.
// La coordinación y su estado pertenecen al hilo principal de Qt.
// Los receptores de CEF encolan copias; los avisos de navegación y descarga se presentan únicamente en Qt.
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
    // Muestra el formulario en Qt. Solo la confirmación del usuario envía las credenciales.
    void OpenLoginDialog(BrowserWindow& window, AuthSession& auth_session, const QString& error_message);

    // Prepara el gestor que utilizará el handler: en modo demo crea contexto y publica el snapshot.
    // true indica que la preparación permite continuar el arranque; el permiso de navegar lo decide el motor.
    [[nodiscard]] bool PrepareDemoPolicy(PolicyManager& manager) const;

    // Consulta el gestor por referencia de lectura y entrega el nombre publicado a los controles Qt.
    // Solo configura la presentación; leer el estado no renueva su vigencia.
    void ConfigureStatus(BrowserWindowUi& window_ui, const PolicyManager& manager) const;

    // Ofrece los presets únicamente en demo y vincula sus solicitudes al contexto inicial.
    // La función guardada conserva el gestor y el cliente CEF, pero no prolonga la vida de la UI.
    void ConfigurePolicySelector(
        BrowserWindowUi& window_ui, const std::shared_ptr<PolicyManager>& manager,
        const CefRefPtr<BrowserClient>& browser_client) const;

    // Publica en el contexto original y solo confirma el cambio visual después del éxito.
    // Después solicita en CEF la reevaluación del documento abierto con la política vigente.
    void ApplyPolicySelection(BrowserWindowUi& window_ui, PolicyManager& manager,
                              const SecurityContext& context, int index,
                              const CefRefPtr<BrowserClient>& browser_client) const;

    // Prepara la función que CEF invocará para solicitar Guardar como.
    // Encola en Qt el ID, el nombre sugerido y la respuesta, sin acceder a controles desde CEF.
    SecurityDownloadHandler::RequestSaveDialogCallback MakeSaveDialogRequestHandler(BrowserWindow& window);

    // Prepara el receptor del aviso de cierre emitido desde CEF al retirar un destino pendiente.
    // Encola el ID en Qt para cerrar solo su diálogo, sin acceder a controles desde el hilo CEF.
    SecurityDownloadHandler::RequestCloseSaveDialogCallback MakeSaveDialogCloseHandler(BrowserWindow& window);

    // Recibe en CEF un bloqueo aplicado y confirma si pudo encolar una copia para Qt.
    SecurityRequestHandler::DecisionCallback MakeNavigationDecisionHandler(
        BrowserWindow& window, const std::shared_ptr<PolicyManager>& manager);

    // Recibe en CEF un bloqueo aplicado y confirma si pudo encolar una copia para Qt.
    SecurityDownloadHandler::DecisionCallback MakeDownloadDecisionHandler(
        BrowserWindow& window, const std::shared_ptr<PolicyManager>& manager);

    // Encola resultados operativos para Qt sin ligarlos a una versión concreta de la política.
    SecurityDownloadHandler::ResultCallback MakeDownloadResultHandler(
        BrowserWindow& window, const std::shared_ptr<PolicyManager>& manager);

    // Descarta avisos obsoletos y presenta el bloqueo en Qt, sin reevaluar permisos de navegación.
    void OnNavigationDecision(const NavigationDecisionNotice& notice, const PolicyManager& manager);

    // Traduce motivos controlados a texto de presentación, sin registrar URL ni datos de navegación.
    static QString NavigationBlockMessage(const NavigationDecisionNotice& notice, const QString& policy_name);

    // Descarta avisos de otro contexto o política y presenta el bloqueo en el hilo Qt.
    void OnDownloadDecision(const DownloadDecisionNotice& notice, const PolicyManager& manager);

    // Traduce los motivos de descarga a texto, sin incluir rutas locales ni nombres recibidos de CEF.
    static QString DownloadBlockMessage(const DownloadDecisionNotice& notice, const QString& policy_name);

    // Presenta el resultado solo si aún pertenece al contexto actual y la ventana sigue abierta.
    void OnDownloadResult(const DownloadResultNotice& notice, const PolicyManager& manager);
    static QString DownloadResultMessage(DownloadOutcome outcome);

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
