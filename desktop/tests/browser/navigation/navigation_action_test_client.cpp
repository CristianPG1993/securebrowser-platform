#include "navigation_action_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sbp {

NavigationActionTestClient::NavigationActionTestClient(
    CefRefPtr<SecurityRequestHandler> handler, std::string parentUrl, std::string targetUrl,
    NavigationTestAction action, int attempts)
    : handler_(std::move(handler)), parentUrl_(std::move(parentUrl)), targetUrl_(std::move(targetUrl)),
      action_(action), attempts_(attempts) {}

NavigationActionTestResult NavigationActionTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationActionTestClient::CreateBrowser,
                                          CefRefPtr<NavigationActionTestClient>(this)))) {
        NavigationActionTestResult failure;
        failure.error = "Could not post action test browser creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("Navigation action test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> NavigationActionTestClient::GetLifeSpanHandler() { return this; }
CefRefPtr<CefRequestHandler> NavigationActionTestClient::GetRequestHandler() { return this; }
CefRefPtr<CefLoadHandler> NavigationActionTestClient::GetLoadHandler() { return this; }

void NavigationActionTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP navigation action test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowserSync(window, this, parentUrl_, settings, nullptr, nullptr)) {
        result_.error = "CEF rejected action test browser creation.";
        Complete();
    }
}

void NavigationActionTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool NavigationActionTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
    bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || !request) {
        result_.error = "Missing main-frame action navigation data.";
        RequestClose(browser);
        return true;
    }
    const bool target = started_ && IsTarget(request->GetURL());
    const std::string url = request->GetURL().ToString();
    const bool source = url == parentUrl_ || (retryPending_ && url == RetrySourceUrl());
    if (!target && !source) {
        result_.error = "Unexpected navigation in action test.";
        RequestClose(browser);
        return true;
    }
    const bool cancel = handler_->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    if (target) {
        ++result_.targetCallbacks;
        if (cancel) {
            ++result_.blockedAttempts;
            if (result_.targetCallbacks < attempts_) {
                retryPending_ = true;
                PostAction(browser);
            } else {
                RequestClose(browser);
            }
        }
    } else if (cancel) {
        result_.error = "The allowed action source was blocked.";
        RequestClose(browser);
    }
    return cancel;
}

void NavigationActionTestClient::OnLoadEnd(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || !frame || !frame->IsMain()) {
        return;
    }
    const std::string url = frame->GetURL().ToString();
    const bool initialSource = !started_ && url == parentUrl_;
    const bool retrySource = retryPending_ && url == RetrySourceUrl();
    if (initialSource || retrySource) {
        if (status != 200) {
            result_.error = "The action source did not return HTTP 200.";
            RequestClose(browser);
            return;
        }
        result_.parentLoaded = true;
        ++result_.sourceLoads;
        started_ = true;
        retryPending_ = false;
        PostAction(browser);
    } else if (IsTarget(frame->GetURL())) {
        result_.targetLoaded = true;
        result_.targetStatus = status;
        RequestClose(browser);
    }
}

void NavigationActionTestClient::PostAction(CefRefPtr<CefBrowser> browser) {
    // La tarea retiene cliente y navegador hasta ejecutar la acción después del callback actual.
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationActionTestClient::LaunchAction,
                                          CefRefPtr<NavigationActionTestClient>(this), browser))) {
        result_.error = "Could not post the navigation action.";
        RequestClose(browser);
    }
}

void NavigationActionTestClient::LaunchAction(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const auto frame = browser->GetMainFrame();
    if (retryPending_) {
        // Una navegación intermedia completada separa los intentos. Chromium puede descartar
        // dos LoadURL idénticos mientras el primero todavía está terminando de cancelarse.
        frame->LoadURL(RetrySourceUrl());
        return;
    }
    if (action_ == NavigationTestAction::Direct) {
        frame->LoadURL(targetUrl_);
        return;
    }
    const auto args = CefListValue::Create();
    args->SetString(0, targetUrl_);
    args->SetInt(1, static_cast<int>(action_));
    const auto json = CefValue::Create();
    json->SetList(args);
    const std::string encoded = CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
    if (encoded.empty()) {
        result_.error = "Could not serialize action script arguments.";
        RequestClose(browser);
        return;
    }
    // El script crea elementos reales y solicita su navegación en el documento permitido.
    const std::string script = R"ACTIONJS((function(target, action) {
    if (action === 1) {
        const link = document.createElement('a');
        link.href = target;
        document.body.appendChild(link);
        link.click();
    } else if (action === 2) {
        window.location.assign(target);
    } else if (action === 3 || action === 4) {
        const form = document.createElement('form');
        form.action = target;
        form.method = action === 4 ? 'POST' : 'GET';
        const field = document.createElement('input');
        field.name = 'private'; field.value = 'fixture-token';
        form.appendChild(field); document.body.appendChild(form); form.submit();
    } else if (action === 5) {
        const blob = new Blob(['<!doctype html><p>Blob fixture.</p>'], {type:'text/html'});
        window.location.assign(URL.createObjectURL(blob));
    }
}).apply(null,)ACTIONJS" + encoded + ");";
    frame->ExecuteJavaScript(script, parentUrl_, 0);
}

bool NavigationActionTestClient::IsTarget(const CefString& url) const {
    if (action_ == NavigationTestAction::Blob) {
        return url.ToString().starts_with("blob:");
    }
    CefURLParts candidate;
    CefURLParts target;
    if (!CefParseURL(url, candidate) || !CefParseURL(targetUrl_, target)) {
        return url.ToString() == targetUrl_;
    }
    return CefString(&candidate.scheme) == CefString(&target.scheme)
        && CefString(&candidate.host) == CefString(&target.host)
        && CefString(&candidate.port) == CefString(&target.port)
        && CefString(&candidate.path) == CefString(&target.path);
}

std::string NavigationActionTestClient::RetrySourceUrl() const {
    return parentUrl_ + "?retry=" + std::to_string(result_.blockedAttempts);
}

void NavigationActionTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> /*frame*/, ErrorCode errorCode,
    const CefString& /*errorText*/, const CefString& /*failedUrl*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_ && !(errorCode == ERR_ABORTED && result_.blockedAttempts > 0)) {
        result_.error = "Action test navigation failed with CEF error "
            + std::to_string(static_cast<int>(errorCode)) + ".";
        RequestClose(browser);
    }
}

void NavigationActionTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) { return; }
    closing_ = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationActionTestClient::CloseBrowser,
                                          CefRefPtr<NavigationActionTestClient>(this), browser))) {
        result_.error = "Could not post action test browser closure.";
        CloseBrowser(browser);
    }
}

void NavigationActionTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void NavigationActionTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationActionTestClient::Complete,
                                          CefRefPtr<NavigationActionTestClient>(this)))) {
        std::fputs("Could not post action test completion.\n", stderr);
        std::abort();
    }
}

void NavigationActionTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    completion_.set_value(result_);
}

}  // namespace sbp
