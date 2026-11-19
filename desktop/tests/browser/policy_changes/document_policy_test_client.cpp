#include "document_policy_test_client.h"

#include "browser/security_download_handler.h"
#include "browser/security_request_handler.h"
#include "demo/demo_policy_source.h"
#include "include/base/cef_callback.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sbp {

DocumentPolicyTestClient::DocumentPolicyTestClient(
    std::shared_ptr<PolicyManager> manager, SecurityContext context, std::string initialUrl,
    std::string entryUrl, DocumentPolicyTestAction action, bool restricted, bool useJavaScript)
    : manager_(std::move(manager)), context_(std::move(context)), initialUrl_(std::move(initialUrl)),
      entryUrl_(std::move(entryUrl)), action_(action), restricted_(restricted), useJavaScript_(useJavaScript),
      notices_(std::make_shared<std::vector<NavigationDecisionNotice>>()) {
    // El receptor posee el vector; la prueba lo lee solo después de cerrar el navegador.
    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager_,
        [notices = notices_](const NavigationDecisionNotice& notice) {
            notices->push_back(notice);
            return true;
        });
    BrowserClientCallbacks callbacks{};
    // Run conserva este observador mientras CEF crea, utiliza y cierra su navegador.
    callbacks.on_browser_created = [this](int) { result_.browserCreated = true; };
    // Se cuentan las señales reales de inicio que Qt utilizaría para limpiar el aviso.
    callbacks.on_main_document_started = [this] { ++result_.documentStartNotices; };
    // BrowserClient confirma el cierre fuera de OnBeforeClose; otra tarea conserva el observador al completar.
    callbacks.on_browser_closed = [this] {
        result_.browserClosed = true;
        if (!CefPostTask(TID_UI, base::BindOnce(&DocumentPolicyTestClient::Complete,
                                              CefRefPtr<DocumentPolicyTestClient>(this)))) {
            std::fputs("Could not post document policy test completion.\n", stderr);
            std::abort();
        }
    };
    // Completa las dependencias del cliente compartiendo el mismo gestor; estas pruebas no solicitan destinos Qt.
    CefRefPtr<SecurityDownloadHandler> download_handler = new SecurityDownloadHandler(manager_);
    client_ = new BrowserClient(std::move(callbacks), handler, download_handler);
}

DocumentPolicyTestResult DocumentPolicyTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&DocumentPolicyTestClient::CreateBrowser,
                                          CefRefPtr<DocumentPolicyTestClient>(this)))) {
        DocumentPolicyTestResult failure;
        failure.error = "Could not post document policy test creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
        std::fputs("Document policy test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> DocumentPolicyTestClient::GetLifeSpanHandler() {
    return client_->GetLifeSpanHandler();
}

CefRefPtr<CefJSDialogHandler> DocumentPolicyTestClient::GetJSDialogHandler() {
    return client_->GetJSDialogHandler();
}

CefRefPtr<CefRequestHandler> DocumentPolicyTestClient::GetRequestHandler() { return this; }
CefRefPtr<CefLoadHandler> DocumentPolicyTestClient::GetLoadHandler() { return this; }
CefRefPtr<CefDisplayHandler> DocumentPolicyTestClient::GetDisplayHandler() { return this; }

void DocumentPolicyTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP document policy test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    const auto browser = CefBrowserHost::CreateBrowserSync(window, this, initialUrl_, settings, nullptr, nullptr);
    if (!browser) {
        result_.error = "CEF rejected document policy test creation.";
        Complete();
        return;
    }
    // El plazo de diagnóstico cierra normalmente el navegador si falta un evento esperado.
    if (!CefPostDelayedTask(TID_UI, base::BindOnce(&DocumentPolicyTestClient::CheckDeadline,
                                                 CefRefPtr<DocumentPolicyTestClient>(this), browser), 5000)) {
        result_.error = "Could not post document policy test deadline.";
        RequestClose(browser);
    }
}

bool DocumentPolicyTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
    bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    const bool cancel = client_->GetRequestHandler()->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    if (phase_ == Phase::Revisiting && frame && frame->IsMain() && request
        && request->GetURL().ToString() == entryUrl_) {
        result_.revokedHistoryBlocked = cancel;
        // La cancelación ocurre en el callback real; la inspección se realiza después, sin forzar su resultado.
        if (!CefPostTask(TID_UI, base::BindOnce(&DocumentPolicyTestClient::ProbeDocument,
                                              CefRefPtr<DocumentPolicyTestClient>(this), browser))) {
            result_.error = "Could not post the revoked-history document probe.";
            RequestClose(browser);
        }
    }
    return cancel;
}

void DocumentPolicyTestClient::OnLoadStart(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType transitionType) {
    client_->OnLoadStart(browser, frame, transitionType);
}

void DocumentPolicyTestClient::OnLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) {
    client_->OnLoadingStateChange(browser, isLoading, canGoBack, canGoForward);
}

void DocumentPolicyTestClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain()) { return; }
    const std::string url = frame->GetURL().ToString();
    if (phase_ == Phase::Initial && url == initialUrl_) {
        if (status != 200) {
            result_.error = "The initial document did not return HTTP 200.";
            RequestClose(browser);
            return;
        }
        result_.initialDocumentLoaded = true;
        phase_ = Phase::CreatingEntry;
        PostAdvance(browser);
    } else if (phase_ == Phase::AwaitingResult && url == "about:blank") {
        result_.neutralDocumentLoaded = true;
        if (action_ == DocumentPolicyTestAction::RevisitAfterRevocation) {
            phase_ = Phase::Revisiting;
            PostAdvance(browser);
        } else {
            ProbeDocument(browser);
        }
    }
}

void DocumentPolicyTestClient::OnAddressChange(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString& url) {
    // Primero actúa el cliente de producción; el observador no contiene decisiones de seguridad.
    client_->OnAddressChange(browser, frame, url);
    if (!frame || !frame->IsMain()) { return; }
    const std::string address = url.ToString();
    if (phase_ == Phase::CreatingEntry && address == entryUrl_) {
        result_.sameDocumentEntryCreated = true;
        OnEntryReady(browser);
    } else if (phase_ == Phase::PreparingForward && address == initialUrl_) {
        phase_ = Phase::Publishing;
        PostAdvance(browser);
    } else if (phase_ == Phase::AwaitingResult && !restricted_
               && address == (action_ == DocumentPolicyTestAction::Back ? initialUrl_ : entryUrl_)) {
        ProbeDocument(browser);
    }
}

void DocumentPolicyTestClient::PostAdvance(CefRefPtr<CefBrowser> browser) {
    // Conserva observador y navegador y permite que termine el callback de dirección o carga actual.
    if (!CefPostTask(TID_UI, base::BindOnce(&DocumentPolicyTestClient::Advance,
                                          CefRefPtr<DocumentPolicyTestClient>(this), browser))) {
        result_.error = "Could not post the next document policy test step.";
        RequestClose(browser);
    }
}

void DocumentPolicyTestClient::Advance(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::CreatingEntry) {
        // Los controles CEF omiten entradas de pushState sin activación de usuario.
        // Runtime.evaluate permite preparar la interacción sin depender del foco de una ventana oculta.
        // Solo se utiliza para crear el historial de la prueba; la decisión sigue en BrowserClient y su handler.
        const auto parameters = CefDictionaryValue::Create();
        parameters->SetBool("userGesture", true);
        parameters->SetString("expression",
            "if(!navigator.userActivation.hasBeenActive){"
            "document.title='SBP history entry inactive';"
            "}else{"
            "window.sbpDocumentMarker=true;"
            "history.pushState({sbp:1},'', '/action-target');"
            "document.title='SBP history entry activated';}");
        if (browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", parameters) == 0) {
            result_.error = "CEF rejected the activated history fixture preparation.";
            RequestClose(browser);
        }
    } else if (phase_ == Phase::PreparingForward || phase_ == Phase::Revisiting) {
        client_->GoBack();
    } else if (phase_ == Phase::Publishing) {
        const auto policy = restricted_ ? DemoPolicySource::CreateRestrictedPolicy() : DemoPolicySource::CreateFreePolicy();
        result_.updatedPolicyPublished = manager_->PublishPolicy(context_, policy) == PolicyPublicationResult::Published;
        if (!result_.updatedPolicyPublished) {
            result_.error = "The updated demo policy was rejected.";
            RequestClose(browser);
            return;
        }
        result_.documentStartNoticesAtUpdate = result_.documentStartNotices;
        result_.actionRequested = true;
        phase_ = Phase::AwaitingResult;
        if (action_ == DocumentPolicyTestAction::Revalidate || action_ == DocumentPolicyTestAction::RevisitAfterRevocation) {
            // Es el mismo punto de entrada que encola el selector Qt tras publicar una política.
            client_->RevalidateCurrentDocument();
            if (!restricted_) { ProbeDocument(browser); }
        } else if (useJavaScript_) {
            browser->GetMainFrame()->ExecuteJavaScript(
                action_ == DocumentPolicyTestAction::Back ? "history.back();" : "history.forward();", initialUrl_, 0);
        } else if (action_ == DocumentPolicyTestAction::Back) {
            client_->GoBack();
        } else {
            client_->GoForward();
        }
    }
}

void DocumentPolicyTestClient::OnEntryReady(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    // La dirección y el título llegan mediante callbacks independientes; se necesitan ambos.
    if (phase_ == Phase::CreatingEntry && result_.sameDocumentEntryCreated
        && result_.entryCreatedWithUserActivation) {
        phase_ = action_ == DocumentPolicyTestAction::Forward ? Phase::PreparingForward : Phase::Publishing;
        PostAdvance(browser);
    }
}

void DocumentPolicyTestClient::ProbeDocument(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Probing || phase_ == Phase::Closing || phase_ == Phase::Finished) { return; }
    phase_ = Phase::Probing;
    // Una navegación interna debe conservar la marca; un documento sustituto debe tener otro contexto JS.
    browser->GetMainFrame()->ExecuteJavaScript(
        "document.title='SBP document probe/'+(window.sbpDocumentMarker===true?'original':'replaced');", "", 0);
}

void DocumentPolicyTestClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    const std::string value = title.ToString();
    if (phase_ == Phase::CreatingEntry) {
        if (value == "SBP history entry activated") {
            result_.entryCreatedWithUserActivation = true;
            OnEntryReady(browser);
        } else if (value == "SBP history entry inactive") {
            result_.error = "The history fixture did not receive user activation.";
            RequestClose(browser);
        }
        return;
    }
    if (phase_ != Phase::Probing) { return; }
    if (value == "SBP document probe/original" || value == "SBP document probe/replaced") {
        result_.documentInspected = true;
        result_.originalDocumentPreserved = value == "SBP document probe/original";
        RequestClose(browser);
    }
}

void DocumentPolicyTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
    const CefString& errorText, const CefString& failedUrl) {
    client_->OnLoadError(browser, frame, errorCode, errorText, failedUrl);
    if (frame && frame->IsMain() && errorCode != ERR_ABORTED) {
        result_.error = "Document policy test load failed: " + std::to_string(static_cast<int>(errorCode));
        RequestClose(browser);
    }
}

void DocumentPolicyTestClient::CheckDeadline(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ != Phase::Closing && phase_ != Phase::Finished) {
        result_.error = "Expected document policy test event was not observed; phase "
            + std::to_string(static_cast<int>(phase_))
            + "; history entry: " + std::to_string(result_.sameDocumentEntryCreated)
            + "; user activation: " + std::to_string(result_.entryCreatedWithUserActivation);
        RequestClose(browser);
    }
}

void DocumentPolicyTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Closing || phase_ == Phase::Finished) { return; }
    phase_ = Phase::Closing;
    browser->GetHost()->CloseBrowser(true);
}

void DocumentPolicyTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    phase_ = Phase::Finished;
    result_.notices = *notices_;
    completion_.set_value(result_);
}

}  // namespace sbp
