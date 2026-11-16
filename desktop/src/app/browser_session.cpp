#include "app/browser_session.h"

#include "app/browser_navigation.h"
#include "app/browser_window_bridge.h"
#include "browser/security_request_handler.h"
#include "demo/demo_policy_source.h"
#include "security/policy_manager.h"
#include "ui/browser_window.h"
#include "ui/browser_window_ui.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <QApplication>
#include <QMetaObject>
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
    std::fprintf(stderr, "Demo policy published; source: local-demo; preset: %s; policy_id: %lld; validity: unlimited.\n",
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

void BrowserSession::ConfigurePolicySelector(
    BrowserWindowUi& window_ui, const std::shared_ptr<PolicyManager>& manager,
    const CefRefPtr<BrowserClient>& browser_client) const {
    if (!startup_options_.IsUrlFilteringDemo()) {
        return;
    }
    const auto state = manager->ReadState();
    if (!state || !state->policy) {
        return;
    }

    // Las opciones muestran los nombres de la fuente; construir datos no los publica ni renueva el plazo.
    const auto free_policy = DemoPolicySource::CreateFreePolicy();
    const auto restricted_policy = DemoPolicySource::CreateRestrictedPolicy();
    window_ui.SetPolicyOptions({QString::fromUtf8(free_policy.Name().c_str()),
                               QString::fromUtf8(restricted_policy.Name().c_str())});
    // La selección inicial procede de la política publicada, no solo del argumento de arranque.
    if (state->policy->Id() == free_policy.Id()) {
        window_ui.SetSelectedPolicy(0);
    } else if (state->policy->Id() == restricted_policy.Id()) {
        window_ui.SetSelectedPolicy(1);
    } else {
        std::fputs("Demo policy selector unavailable: unknown published policy.\n", stderr);
        return;
    }

    // Se ejecuta en Qt: la UI posee esta función; manager y browser_client se conservan por valor.
    // La copia del contexto impide que una solicitud tardía se aplique a otro contexto posterior.
    window_ui.SetPolicySelectionHandler([this, &window_ui, manager, browser_client, context = state->context](int index) {
        ApplyPolicySelection(window_ui, *manager, context, index, browser_client);
    });
}

void BrowserSession::ApplyPolicySelection(BrowserWindowUi& window_ui, PolicyManager& manager,
                                         const SecurityContext& context, int index,
                                         const CefRefPtr<BrowserClient>& browser_client) const {
    // Solo una elección conocida en la sesión demo puede cambiar una política desde estos controles.
    if (!startup_options_.IsUrlFilteringDemo() || lifecycle_.CloseRequested()
        || (index != 0 && index != 1)) {
        return;
    }
    if (!manager.IsCurrentContext(context)) {
        std::fputs("Demo policy selection rejected: the original context is no longer current.\n", stderr);
        return;
    }

    const auto policy = index == 0 ? DemoPolicySource::CreateFreePolicy()
                                   : DemoPolicySource::CreateRestrictedPolicy();
    // El mismo gestor que consulta CEF valida y publica el snapshot; el motor conserva todas sus guardas.
    const auto result = manager.PublishPolicy(context, policy);
    if (result != PolicyPublicationResult::Published) {
        std::fputs("Demo policy selection rejected: local demo policy publication failed.\n", stderr);
        return;
    }

    window_ui.SetPolicyName(QString::fromUtf8(policy.Name().c_str()));
    window_ui.SetSelectedPolicy(index);
    // El mensaje de la política anterior ya no corresponde al estado que acaba de publicarse.
    window_ui.ClearNavigationBlock();
    std::fprintf(stderr, "Demo policy changed; source: local-demo; preset: %s; policy_id: %lld; validity: unlimited.\n",
                 index == 0 ? "free" : "restricted", static_cast<long long>(policy.Id()));
    // La publicación ya terminó. La tarea conserva el cliente y consulta la política vigente al ejecutarse.
    // Los objetos CefBrowser y CefFrame se obtienen y utilizan únicamente dentro del hilo UI de CEF.
    if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::RevalidateCurrentDocument, browser_client))) {
        std::fputs("Demo policy changed, but current-document revalidation could not be posted to CEF.\n", stderr);
    }
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
        // La demo autónoma no caduca; el modo normal conserva el límite monotónico de 15 minutos.
        const auto policy_manager = std::make_shared<PolicyManager>(startup_options_.IsUrlFilteringDemo()
            ? PolicyManager::Validity::Unlimited : PolicyManager::Validity::FifteenMinutes);
        if (!PrepareDemoPolicy(*policy_manager)) {
            return 1;
        }
        // security_handler recibe OnBeforeBrowse, consulta el motor y cancela las navegaciones bloqueadas.
        // El receptor copia el aviso desde CEF y lo encola para presentarlo únicamente en Qt.
        CefRefPtr<SecurityRequestHandler> security_handler = new SecurityRequestHandler(
            policy_manager, MakeNavigationDecisionHandler(window, policy_manager));
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
        // El selector solo se conecta en demo; cada elección explícita publica en el mismo contexto.
        ConfigurePolicySelector(window_ui, policy_manager, browser_client);
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

        // Retira el receptor del selector mientras los controles y el gestor todavía siguen vivos.
        window_ui.SetPolicySelectionHandler({});

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

std::function<bool(const NavigationDecisionNotice&)> BrowserSession::MakeNavigationDecisionHandler(
    BrowserWindow& window, const std::shared_ptr<PolicyManager>& manager) {
    // CEF ejecuta este receptor; window vive hasta la confirmación de cierre y manager se conserva por valor.
    return [this, &window, manager](const NavigationDecisionNotice& notice) {
        std::fprintf(stderr, "CEF navigation blocked; operation_id: %llu; reason: %d; main_frame: %d.\n",
                     static_cast<unsigned long long>(notice.operationId),
                     static_cast<int>(notice.decision.Reason()), static_cast<int>(notice.mainFrame));
        // Esta lectura no renueva la vigencia; permite detectar cualquier publicación posterior al envío.
        const auto observed_state = manager->ReadState();
        // La tarea posee el aviso y el estado observado; solo aquí se consulta la vida de los controles Qt.
        return QMetaObject::invokeMethod(&window, [this, manager, notice, observed_state] {
            if (manager->ReadState() != observed_state) {
                return;
            }
            OnNavigationDecision(notice, *manager);
        }, Qt::QueuedConnection);
    };
}

void BrowserSession::OnNavigationDecision(const NavigationDecisionNotice& notice, const PolicyManager& manager) {
    if (lifecycle_.CloseRequested() || window_ui_ == nullptr
        || notice.operationId <= last_notice_operation_id_
        || notice.decision.Action() != SecurityAction::Block) {
        return;
    }
    last_notice_operation_id_ = notice.operationId;

    // El contexto y el ID deben corresponder al bloqueo recibido, no a otra sesión o política posterior.
    const auto state = manager.ReadState();
    if (notice.context.has_value()) {
        if (!state || !(state->context == *notice.context)) {
            return;
        }
    } else if (state) {
        return;
    }
    if (notice.policyId.has_value()) {
        if (!state || !state->policy || state->policy->Id() != *notice.policyId) {
            return;
        }
    } else if (state && state->policy) {
        return;
    }
    // Una republicación de la misma política puede haber renovado el plazo antes de entregar el aviso.
    if (notice.decision.Reason() == SecurityReason::PolicyExpired && state
        && manager.ReadPolicy(state->context).availability != PolicyAvailability::Expired) {
        return;
    }

    const QString policy_name = state && state->policy
        ? QString::fromUtf8(state->policy->Name().c_str()) : QString{};
    window_ui_->ShowNavigationBlock(NavigationBlockMessage(notice, policy_name));
    std::fprintf(stderr, "Qt navigation block notice shown; operation_id: %llu.\n",
                 static_cast<unsigned long long>(notice.operationId));
}

QString BrowserSession::NavigationBlockMessage(const NavigationDecisionNotice& notice, const QString& policy_name) {
    QString reason;
    switch (notice.decision.Reason()) {
    case SecurityReason::InvalidContext:
        reason = QStringLiteral("No hay un contexto de seguridad válido para esta navegación.");
        break;
    case SecurityReason::InstallationUnauthorized:
        reason = QStringLiteral("La instalación no está autorizada para navegar.");
        break;
    case SecurityReason::PolicyUnavailable:
        reason = QStringLiteral("No hay una política disponible. Es necesario cargar una política válida.");
        break;
    case SecurityReason::PolicyExpired:
        reason = QStringLiteral("La política ha caducado tras 15 minutos. Es necesario volver a cargarla.");
        break;
    case SecurityReason::InvalidUrl:
        reason = QStringLiteral("La dirección no se puede interpretar de forma válida.");
        break;
    case SecurityReason::UnsupportedScheme:
        reason = QStringLiteral("El tipo de dirección no está permitido.");
        break;
    case SecurityReason::IpBlocked:
        reason = QStringLiteral("La política no permite navegar a direcciones IP literales.");
        break;
    case SecurityReason::DomainDenied:
        reason = QStringLiteral("El dominio %1 está bloqueado por la política.")
            .arg(QString::fromUtf8(notice.host.c_str()));
        break;
    case SecurityReason::DomainNotAllowed:
        reason = QStringLiteral("El dominio %1 no está autorizado por la política.")
            .arg(QString::fromUtf8(notice.host.c_str()));
        break;
    default:
        reason = QStringLiteral("La navegación no supera las comprobaciones de seguridad.");
        break;
    }
    QString message = notice.mainFrame
        ? QStringLiteral("Navegación bloqueada. %1").arg(reason)
        : QStringLiteral("Contenido de la página bloqueado. %1").arg(reason);
    if (!policy_name.isEmpty()) {
        message += QStringLiteral("\nPolítica: %1").arg(policy_name);
    }
    return message;
}

}  // namespace sbp
