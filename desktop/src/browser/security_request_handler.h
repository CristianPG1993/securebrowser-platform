#pragma once

#include "browser/cef_url_adapter.h"
#include "browser/navigation_decision_notice.h"
#include "security/policy_manager.h"
#include "security/url_filter.h"

#include "include/cef_request_handler.h"

#include <memory>
#include <functional>

namespace sbp {

// Conecta las navegaciones de CEF con el estado local y el evaluador de URL.
// El aviso se emite solo al cancelar en UI; los callbacks de recursos no duplican el bloqueo.
class SecurityRequestHandler final : public CefRequestHandler {
public:
    // Crea un handler que comparte el gestor recibido y lo mantiene vivo mientras lo utiliza.
    // Consultará la política vigente; no guarda una copia fija de sus reglas.
    // El receptor se ejecuta en UI CEF, debe ser breve y comunicar si acepta el aviso.
    using DecisionCallback = std::function<bool(const NavigationDecisionNotice&)>;
    explicit SecurityRequestHandler(std::shared_ptr<PolicyManager> policyManager,
                                    DecisionCallback onDecision = {});

    // Prohíbe crear un handler copiando otro. Sí permite construir otros handlers independientes.
    SecurityRequestHandler(const SecurityRequestHandler&) = delete;

    // Prohíbe copiar el contenido de un handler sobre otro existente; no es una comparación.
    SecurityRequestHandler& operator=(const SecurityRequestHandler&) = delete;

    // CEF lo llama en su hilo UI antes de navegar en el documento principal o un subframe.
    // true cancela la navegación; false permite continuar.
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool user_gesture,
                        bool is_redirect) override;

    // Reevalúa la URL del documento abierto con la política vigente, en el hilo UI de CEF.
    // true exige retirar el documento; este método comunica el bloqueo pero no navega por sí solo.
    [[nodiscard]] bool ShouldBlockDocument(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame);

private:
    // Destruye el handler al desaparecer el último CefRefPtr; el código externo no hace delete.
    ~SecurityRequestHandler() override = default;

    // Reconoce solo about:blank y, fuera del documento principal, about:srcdoc.
    // Una navegación web originada por esos documentos seguirá sometida al evaluador.
    [[nodiscard]] static bool IsAllowedInternalNavigation(const CefString& url, bool isMainFrame);

    // Comparte las mismas guardas para una petición nueva y para un documento ya existente.
    // true indica bloqueo; la decisión continúa perteneciendo a PolicyManager y UrlFilter.
    [[nodiscard]] bool ShouldBlockUrl(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                      const CefString& url, bool isRedirect);

    // La entrega fallida se diagnostica y nunca convierte una cancelación en permiso.
    void NotifyBlocked(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       const UrlTarget& target, SecurityReason reason, bool isRedirect,
                       const std::shared_ptr<const PolicyManager::State>& state);

    const std::shared_ptr<PolicyManager> policyManager_;
    const DecisionCallback onDecision_;
    // Solo se incrementa en el hilo UI de CEF y no depende de la URL ni del tiempo.
    static std::uint64_t lastOperationId_;
    CefUrlAdapter urlAdapter_;
    UrlFilter urlFilter_;

    // Añade el contador de CEF: varios CefRefPtr comparten el mismo objeto sin copiarlo.
    IMPLEMENT_REFCOUNTING(SecurityRequestHandler);
};

}  // namespace sbp
