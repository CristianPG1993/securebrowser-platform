#pragma once

#include "browser/cef_url_adapter.h"
#include "security/policy_manager.h"
#include "security/url_filter.h"

#include "include/cef_request_handler.h"

#include <memory>

namespace sbp {

// Conecta las navegaciones de CEF con el estado local y el evaluador de URL.
// La cobertura de recursos y redirecciones se incorporará y verificará por separado.
class SecurityRequestHandler final : public CefRequestHandler {
public:
    // Crea un handler que comparte el gestor recibido y lo mantiene vivo mientras lo utiliza.
    // Consultará la política vigente; no guarda una copia fija de sus reglas.
    explicit SecurityRequestHandler(std::shared_ptr<PolicyManager> policyManager);

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

private:
    // Destruye el handler al desaparecer el último CefRefPtr; el código externo no hace delete.
    ~SecurityRequestHandler() override = default;

    // Reconoce solo about:blank y, fuera del documento principal, about:srcdoc.
    // Una navegación web originada por esos documentos seguirá sometida al evaluador.
    [[nodiscard]] static bool IsAllowedInternalNavigation(const CefString& url, bool isMainFrame);

    const std::shared_ptr<PolicyManager> policyManager_;
    CefUrlAdapter urlAdapter_;
    UrlFilter urlFilter_;

    // Añade el contador de CEF: varios CefRefPtr comparten el mismo objeto sin copiarlo.
    IMPLEMENT_REFCOUNTING(SecurityRequestHandler);
};

}  // namespace sbp
