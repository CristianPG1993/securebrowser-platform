#include "browser/security_request_handler.h"

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

#include <stdexcept>
#include <utility>

namespace sbp {

// Conserva la propiedad compartida del gestor y rechaza una dependencia inexistente.
SecurityRequestHandler::SecurityRequestHandler(std::shared_ptr<PolicyManager> policyManager)
    : policyManager_(std::move(policyManager)) {
    if (!policyManager_) {
        throw std::invalid_argument("SecurityRequestHandler requires a policy manager.");
    }
}

// Aplica el control previo en el hilo UI; true indica a CEF que debe cancelar esta navegación.
bool SecurityRequestHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          CefRefPtr<CefRequest> request,
                                          bool /*user_gesture*/,
                                          bool /*is_redirect*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!browser || !frame || !request) {
        return true;
    }

    const CefString url = request->GetURL();
    if (IsAllowedInternalNavigation(url, frame->IsMain())) {
        return false;
    }

    // Conserva el contexto que originó esta evaluación, aunque el gestor publique otro después.
    const auto state = policyManager_->ReadState();
    if (!state) {
        return true;
    }

    const UrlTarget target = urlAdapter_.Parse(url);
    const auto policy = policyManager_->ReadPolicy(state->context);
    const SecurityDecision decision = urlFilter_.Evaluate(policy, target);
    if (decision.Action() == SecurityAction::Block) {
        return true;
    }

    // Un cambio de sesión o instalación durante la evaluación impide aplicar el permiso anterior.
    return !policyManager_->IsCurrentContext(state->context);
}

// Identifica documentos internos concretos sin admitir globalmente el esquema about.
bool SecurityRequestHandler::IsAllowedInternalNavigation(const CefString& url, bool isMainFrame) {
    if (url.empty()) {
        return false;
    }

    CefURLParts parts;
    if (!CefParseURL(url, parts)) {
        return false;
    }

    if (CefString(&parts.scheme).ToString() != "about"
        || parts.host.length != 0 || parts.port.length != 0
        || parts.username.length != 0 || parts.password.length != 0) {
        return false;
    }

    const std::string path = CefString(&parts.path).ToString();
    return path == "blank" || (!isMainFrame && path == "srcdoc");
}

}  // namespace sbp
