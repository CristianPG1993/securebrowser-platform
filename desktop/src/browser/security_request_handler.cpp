#include "browser/security_request_handler.h"

#include "include/cef_parser.h"
#include "include/wrapper/cef_helpers.h"

#include <stdexcept>
#include <cstdio>
#include <limits>
#include <utility>

namespace sbp {

std::uint64_t SecurityRequestHandler::lastOperationId_ = 0;

// Conserva la propiedad compartida del gestor y rechaza una dependencia inexistente.
SecurityRequestHandler::SecurityRequestHandler(std::shared_ptr<PolicyManager> policyManager,
                                               DecisionCallback onDecision)
    : policyManager_(std::move(policyManager)), onDecision_(std::move(onDecision)) {
    if (!policyManager_) {
        throw std::invalid_argument("SecurityRequestHandler requires a policy manager.");
    }
}

// Aplica el control previo en el hilo UI; true indica a CEF que debe cancelar esta navegación.
bool SecurityRequestHandler::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefFrame> frame,
                                          CefRefPtr<CefRequest> request,
                                          bool /*user_gesture*/,
                                          bool is_redirect) {
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
    const UrlTarget target = urlAdapter_.Parse(url);
    if (!state) {
        NotifyBlocked(browser, frame, target, SecurityReason::InvalidContext, is_redirect, nullptr);
        return true;
    }

    const auto policy = policyManager_->ReadPolicy(state->context);
    const SecurityDecision decision = urlFilter_.Evaluate(policy, target);
    const auto evaluatedState = policy.state && policy.state->context == state->context
        ? policy.state : state;
    if (decision.Action() == SecurityAction::Block) {
        NotifyBlocked(browser, frame, target, decision.Reason(), is_redirect, evaluatedState);
        return true;
    }

    // Un cambio de sesión o instalación durante la evaluación impide aplicar el permiso anterior.
    if (!policyManager_->IsCurrentContext(state->context)) {
        NotifyBlocked(browser, frame, target, SecurityReason::InvalidContext, is_redirect, evaluatedState);
        return true;
    }
    return false;
}

void SecurityRequestHandler::NotifyBlocked(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const UrlTarget& target,
    SecurityReason reason, bool isRedirect, const std::shared_ptr<const PolicyManager::State>& state) {
    CEF_REQUIRE_UI_THREAD();
    if (lastOperationId_ == std::numeric_limits<std::uint64_t>::max()) {
        std::fputs("CEF navigation blocked; decision correlation counter exhausted.\n", stderr);
        return;
    }
    NavigationDecisionNotice notice;
    notice.operationId = ++lastOperationId_;
    notice.occurredAt = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now());
    notice.browserId = browser->GetIdentifier();
    notice.frameId = frame->GetIdentifier().ToString();
    notice.mainFrame = frame->IsMain();
    notice.redirect = isRedirect;
    notice.host = target.Host();
    notice.decision = SecurityDecision(reason);
    if (state) {
        notice.context = state->context;
        if (state->policy) {
            notice.policyId = state->policy->Id();
        }
    }
    try {
        if (!onDecision_ || !onDecision_(notice)) {
            std::fputs("CEF navigation blocked; decision notice was not accepted.\n", stderr);
        }
    } catch (...) {
        // No se imprime el mensaje de una excepción ajena: podría contener datos de la petición.
        std::fputs("CEF navigation blocked; decision receiver failed.\n", stderr);
    }
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
