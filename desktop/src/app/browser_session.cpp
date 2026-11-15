#include "app/browser_session.h"

#include "app/browser_navigation.h"
#include "app/browser_window_bridge.h"
#include "browser/security_request_handler.h"
#include "demo/demo_policy_source.h"
#include "security/policy_manager.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include <QApplication>
#include <QString>

#include <cstdio>
#include <memory>

namespace sbp {

BrowserSession::BrowserSession(const StartupOptions& startup_options)
    : startup_options_(startup_options) {}

bool BrowserSession::PrepareDemoPolicy(PolicyManager& manager) const {
    if (!startup_options_.IsUrlFilteringDemo()) {
        // El modo normal conserva el gestor vacío hasta la futura adquisición autenticada.
        return true;
    }

    // context identifica esta sesión demo; manager es el mismo objeto que compartirá el handler.
    const auto context = manager.BeginContext(DemoPolicySource::DeviceId, DemoPolicySource::CompanyId);
    if (!context.has_value()) {
        std::fputs("Demo startup failed: could not create the local demo context.\n", stderr);
        return false;
    }

    // La selección procede de StartupOptions, que solo admite estos dos presets conocidos.
    const bool restricted = startup_options_.InitialDemoPolicy() == DemoPolicyPreset::RestrictedNavigation;
    // policy es el snapshot completo de datos; PublishPolicy lo validará antes de aplicarlo.
    const auto policy = restricted ? DemoPolicySource::CreateRestrictedPolicy()
                                   : DemoPolicySource::CreateFreePolicy();
    const auto result = manager.PublishPolicy(*context, policy);
    if (result != PolicyPublicationResult::Published) {
        std::fputs("Demo startup failed: local demo policy publication was rejected.\n", stderr);
        return false;
    }

    // Solo registra datos controlados y deja explícita la procedencia local, sin afirmar autenticación.
    std::fprintf(stderr, "Demo policy published; source: local-demo; preset: %s; policy_id: %lld.\n",
                 restricted ? "restricted" : "free", static_cast<long long>(policy.Id()));
    return true;
}

void BrowserSession::ConfigureStatus(BrowserWindowUi& window_ui, const PolicyManager& manager) const {
    if (!startup_options_.IsUrlFilteringDemo()) {
        return;
    }
    // state conserva el estado de solo lectura: contexto, snapshot e instante de validación.
    const auto state = manager.ReadState();
    // policy_name es únicamente el texto del panel, convertido desde el nombre UTF-8 del snapshot.
    const QString policy_name = state && state->policy
        ? QString::fromUtf8(state->policy->Name().c_str()) : QStringLiteral("No disponible");
    window_ui.SetPolicyName(policy_name);
}

void BrowserSession::OnContextInitialized() {
    lifecycle_.OnContextInitialized();
}

// Paso 1: Entramos en la sesión desde el hilo principal, utilizando el QApplication ya creado.
int BrowserSession::Run(QApplication& qt_app) {
    // Paso 2: Preparamos el código que devolverá la sesión; 0 representa una salida correcta.
    int qt_exit_code = 0;
    // Paso 3: Abrimos un ámbito para destruir sus objetos locales antes de regresar al arranque CEF.
    {
        // Paso 4: window es la ventana principal Qt; recibe los eventos y contiene los controles.
        // Todavía no se muestra en pantalla.
        BrowserWindow window;
        // Paso 4.1: navigation conecta barra y botones Qt con las tareas de navegación de CEF.
        // También prepara las funciones que trasladan dirección e historial desde CEF a Qt.
        BrowserNavigation navigation;
        // Paso 4.2: policy_manager conserva contexto, snapshot y vigencia; el handler comparte este gestor.
        // La política local se prepara una vez al arrancar, solo si se activó expresamente la demo.
        const auto policy_manager = std::make_shared<PolicyManager>();
        if (!PrepareDemoPolicy(*policy_manager)) {
            return 1;
        }
        // security_handler recibe OnBeforeBrowse, consulta el motor y cancela las navegaciones bloqueadas.
        // OnNavigationDecision recibirá los avisos de bloqueo aplicado en el hilo UI de CEF.
        CefRefPtr<SecurityRequestHandler> security_handler = new SecurityRequestHandler(
            policy_manager, &BrowserSession::OnNavigationDecision);
        // Paso 5: browser_client es el cliente que entregamos a CEF para crear y manejar el navegador.
        // Recibe creación, carga, historial y cierre; GetRequestHandler entrega security_handler a CEF.
        // MakeCallbacks agrupa los avisos de navegación; MakeClientCallbacks añade creación y cierre.
        // El constructor guarda esas funciones: sus avisos se ejecutarán después; aún no creamos el navegador CEF.
        CefRefPtr<BrowserClient> browser_client(new BrowserClient(
            lifecycle_.MakeClientCallbacks(
                window, navigation.MakeCallbacks(window, window_ui_, lifecycle_.CloseRequested())),
            security_handler));

        std::fputs("CEF browser client prepared.\n", stderr);

        // Paso 6: Guardamos las funciones que solicitarán la creación y el cierre; aquí no se ejecutan.
        lifecycle_.Connect(window, browser_client);

        // Paso 7: window_bridge sincroniza tamaño y foco entre el anfitrión Qt y la ventana hija CEF.
        // Conserva acceso al cliente y a los datos de cierre/ventana nativa que mantiene lifecycle_.
        BrowserWindowBridge window_bridge(browser_client, lifecycle_.CloseRequested(), lifecycle_.HostHandle());
        // Paso 8: window_ui construye barra, botones, anfitrión CEF y panel inferior de estado.
        // Muestra los datos recibidos y comunica acciones; las decisiones de seguridad pertenecen al motor.
        BrowserWindowUi window_ui(window, window_bridge.MakeHostCallbacks());
        // Paso 9: Guardamos la dirección de los controles para que el aviso de URL pueda encontrarlos en Qt.
        window_ui_ = &window_ui;
        // El panel recibe la política efectivamente publicada, antes de mostrar la ventana.
        ConfigureStatus(window_ui, *policy_manager);
        // Paso 10: Instalamos las acciones de navegación que enviarán las peticiones de Qt al hilo UI de CEF.
        navigation.Connect(window_ui, browser_client, lifecycle_.CloseRequested());
        // Paso 11: Instalamos la sincronización del foco nativo del marco con los controles y el contenido web.
        window_bridge.Connect(window, window_ui);
        // Paso 12: Show muestra la ventana, prepara el foco y devuelve el HWND del anfitrión.
        // OnHostReady guarda ese HWND y solicita crear el navegador cuando el contexto CEF también esté listo.
        lifecycle_.OnHostReady(window_bridge.Show(window, window_ui));

        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);

        // Paso 13: exec procesa los eventos Qt hasta terminar la sesión; CEF mantiene su propio hilo UI.
        // Cuando exec retorna, ExitCode conserva un error previo o utiliza el resultado del bucle Qt.
        qt_exit_code = lifecycle_.ExitCode(qt_app.exec());

        // Paso 14: Liberamos la función de creación y limpiamos los datos del anfitrión, aún con los widgets vivos.
        lifecycle_.Detach();
        // Paso 15: Retiramos el acceso a los controles para descartar actualizaciones posteriores de dirección.
        window_ui_ = nullptr;
        // Paso 16: Retiramos el callback de foco nativo antes de destruir los controles que utilizaba.
        window_bridge.Disconnect(window);
        // Paso 17: Al terminar este ámbito se destruyen sus objetos locales y se liberan sus referencias.
    }


    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    // Paso 18: Devolvemos el resultado al arranque, que después llamará a CefShutdown.
    return qt_exit_code;
}

bool BrowserSession::OnNavigationDecision(const NavigationDecisionNotice& notice) {
    // El aviso pertenece a UI CEF. No accede a Qt ni afirma persistencia o entrega al backend.
    std::fprintf(stderr, "CEF navigation blocked; operation_id: %llu; reason: %d; main_frame: %d.\n",
                 static_cast<unsigned long long>(notice.operationId),
                 static_cast<int>(notice.decision.Reason()), static_cast<int>(notice.mainFrame));
    return true;
}

}  // namespace sbp
