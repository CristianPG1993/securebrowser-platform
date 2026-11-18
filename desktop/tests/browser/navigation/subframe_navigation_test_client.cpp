#include "subframe_navigation_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_request.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sbp {

SubframeNavigationTestClient::SubframeNavigationTestClient(
    CefRefPtr<SecurityRequestHandler> handler, std::string parentUrl, std::string childUrl,
    std::string expectedChildUrl, std::string expectedMarker)
    : handler_(std::move(handler)), parentUrl_(std::move(parentUrl)), childUrl_(std::move(childUrl)),
      expectedChildUrl_(std::move(expectedChildUrl)), expectedMarker_(std::move(expectedMarker)),
      internalChild_(expectedChildUrl_ == "about:blank" || expectedChildUrl_ == "about:srcdoc") {}

SubframeNavigationTestResult SubframeNavigationTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&SubframeNavigationTestClient::CreateBrowser,
                                          CefRefPtr<SubframeNavigationTestClient>(this)))) {
        SubframeNavigationTestResult failure;
        failure.error = "Could not post subframe test browser creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("Subframe navigation test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> SubframeNavigationTestClient::GetLifeSpanHandler() { return this; }
CefRefPtr<CefRequestHandler> SubframeNavigationTestClient::GetRequestHandler() { return this; }
CefRefPtr<CefLoadHandler> SubframeNavigationTestClient::GetLoadHandler() { return this; }
CefRefPtr<CefDisplayHandler> SubframeNavigationTestClient::GetDisplayHandler() { return this; }

void SubframeNavigationTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP subframe navigation test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowserSync(window, this, parentUrl_, settings, nullptr, nullptr)) {
        result_.error = "CEF rejected subframe test browser creation.";
        Complete();
    }
}

void SubframeNavigationTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool SubframeNavigationTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !request) {
        result_.error = "Missing frame navigation data.";
        RequestClose(browser);
        return true;
    }
    const std::string url = request->GetURL().ToString();
    const bool cancel = handler_->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    Trace(frame->IsMain() ? "main" : cancel ? "child:block" : "child:allow", request->GetURL());
    if (frame->IsMain()) {
        result_.parentCallbackObserved = true;
        if (url != parentUrl_ || cancel) {
            result_.error = "The allowed parent navigation did not remain available.";
            RequestClose(browser);
            return true;
        }
    } else {
        // Los documentos internos iniciales de un iframe no cuentan como navegación web.
        if (url != "about:blank" && url != "about:srcdoc") {
            ++result_.childWebCallbacks;
            if (isRedirect) {
                ++result_.childRedirectCallbacks;
            }
        }
        if (url == expectedChildUrl_) {
            result_.childCallbackObserved = true;
        }
        if (cancel) {
            result_.childBlocked = true;
            if (url != expectedChildUrl_) {
                result_.error = "An unexpected child navigation was blocked.";
            }
            RequestClose(browser);
        }
    }
    // La prueba no fuerza una cancelación en función de su resultado esperado.
    return cancel;
}

void SubframeNavigationTestClient::OnLoadEnd(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || !frame) {
        return;
    }
    const std::string url = frame->GetURL().ToString();
    if (frame->IsMain()) {
        if (url != parentUrl_ || httpStatusCode != 200) {
            result_.error = "The parent document did not complete with HTTP 200.";
            RequestClose(browser);
            return;
        }
        result_.parentLoaded = true;
        if (!result_.childStarted) {
            result_.childStarted = true;
            if (!CefPostTask(TID_UI, base::BindOnce(&SubframeNavigationTestClient::StartChild,
                                                  CefRefPtr<SubframeNavigationTestClient>(this), browser))) {
                result_.error = "Could not post child navigation.";
                RequestClose(browser);
            }
        }
        return;
    }
    if (url != expectedChildUrl_) {
        return;
    }
    result_.childLoaded = true;
    result_.childLoadEndObserved = true;
    result_.childStatus = httpStatusCode;
    if (!internalChild_) {
        // Solo el frame que acaba de cargar comprueba su contenido y avisa a su padre.
        const std::string script = "parent.postMessage(document.body && document.body.textContent.includes("
            + JavaScriptString(expectedMarker_)
            + ") ? 'SBP child verified' : 'SBP child mismatch', '*');";
        frame->ExecuteJavaScript(script, frame->GetURL(), 0);
    }
}

void SubframeNavigationTestClient::StartChild(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    // La página padre conserva el handle del iframe y solo acepta mensajes de ese documento hijo.
    const std::string script = R"FRAMEJS((function(source, expected, marker, internal, srcdoc) {
    const child = document.createElement('iframe');
    child.name = 'security-child';
    window.addEventListener('message', function(event) {
        if (event.source !== child.contentWindow) return;
        if (event.data === 'SBP child verified') document.title = 'SBP subframe verified';
        if (event.data === 'SBP child mismatch') document.title = 'SBP subframe mismatch';
    });
    if (internal) {
        child.addEventListener('load', function() {
            let verified = false;
            try {
                const body = child.contentWindow.document.body;
                verified = child.contentWindow.location.href === expected && body &&
                    (marker ? body.textContent.includes(marker) : body.textContent.trim() === '');
            } catch (error) {}
            document.title = verified ? 'SBP subframe verified' : 'SBP subframe mismatch';
        });
    }
    if (srcdoc) child.srcdoc = '<!doctype html><p>SBP srcdoc fixture</p>';
    else child.src = source;
    document.body.appendChild(child);
})()FRAMEJS" + JavaScriptString(childUrl_) + "," + JavaScriptString(expectedChildUrl_)
        + "," + JavaScriptString(expectedMarker_) + "," + (internalChild_ ? "true" : "false")
        + "," + (expectedChildUrl_ == "about:srcdoc" ? "true" : "false") + ");";
    browser->GetMainFrame()->ExecuteJavaScript(script, parentUrl_, 0);
}

void SubframeNavigationTestClient::OnTitleChange(
    CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const std::string value = title.ToString();
    if (value == "SBP subframe mismatch") {
        result_.error = "The child document did not contain its expected fixture.";
        RequestClose(browser);
    } else if (value == "SBP subframe verified") {
        result_.childDocumentVerified = true;
        if (internalChild_) {
            VerifyInternalChild(browser);
        } else {
            RequestClose(browser);
        }
    }
}

void SubframeNavigationTestClient::VerifyInternalChild(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    const auto frame = browser->GetFrameByName("security-child");
    if (!frame || frame->IsMain() || frame->GetURL().ToString() != expectedChildUrl_) {
        result_.error = "The internal document was not found in the expected child frame.";
    } else {
        // El evento load y la comprobación DOM acreditan la carga aunque CEF omita OnLoadEnd.
        result_.childLoaded = true;
        if (!result_.childCallbackObserved) {
            // Igual que about:blank principal: una llamada directa no se presenta como callback de CEF.
            const auto request = CefRequest::Create();
            request->SetURL(expectedChildUrl_);
            result_.childBlocked = handler_->OnBeforeBrowse(browser, frame, request, false, false);
            result_.internalHandlerCheckedDirectly = true;
        }
    }
    RequestClose(browser);
}

void SubframeNavigationTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> /*frame*/, ErrorCode errorCode,
    const CefString& /*errorText*/, const CefString& /*failedUrl*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_) {
        result_.error = "Subframe test navigation failed with CEF error "
            + std::to_string(static_cast<int>(errorCode)) + ".";
        RequestClose(browser);
    }
}

void SubframeNavigationTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    closing_ = true;
    result_.parentPreserved = browser->GetMainFrame()->GetURL().ToString() == parentUrl_;
    if (!CefPostTask(TID_UI, base::BindOnce(&SubframeNavigationTestClient::CloseBrowser,
                                          CefRefPtr<SubframeNavigationTestClient>(this), browser))) {
        result_.error = "Could not post subframe test browser closure.";
        CloseBrowser(browser);
    }
}

void SubframeNavigationTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void SubframeNavigationTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&SubframeNavigationTestClient::Complete,
                                          CefRefPtr<SubframeNavigationTestClient>(this)))) {
        std::fputs("Could not post subframe test completion.\n", stderr);
        std::abort();
    }
}

void SubframeNavigationTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    completion_.set_value(result_);
}

std::string SubframeNavigationTestClient::JavaScriptString(const std::string& value) {
    // CefWriteJSON recibe un diccionario o lista; se retiran solo los corchetes del contenedor.
    const auto list = CefListValue::Create();
    list->SetString(0, value);
    const auto json = CefValue::Create();
    json->SetList(list);
    const std::string encoded = CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
    if (encoded.size() < 2) {
        std::fputs("Could not serialize subframe fixture script argument.\n", stderr);
        std::abort();
    }
    return encoded.substr(1, encoded.size() - 2);
}

void SubframeNavigationTestClient::Trace(const std::string& stage, const CefString& url) {
    CEF_REQUIRE_UI_THREAD();
    CefURLParts parts;
    if (CefParseURL(url, parts)) {
        const std::string host = CefString(&parts.host).ToString();
        result_.trace.push_back(stage + " " + (host.empty() ? CefString(&parts.scheme).ToString() : host));
    }
}

}  // namespace sbp
