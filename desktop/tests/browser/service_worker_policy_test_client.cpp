#include "service_worker_policy_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace sbp {

ServiceWorkerPolicyTestClient::ServiceWorkerPolicyTestClient(
    std::shared_ptr<PolicyManager> manager, SecurityContext context,
    std::string origin, bool blockTarget)
    : manager_(std::move(manager)), context_(std::move(context)),
      handler_(new SecurityRequestHandler(manager_)), startupUrl_(origin + "/sw-start"),
      targetUrl_(origin + "/sw-target"), blockTarget_(blockTarget) {}

ServiceWorkerPolicyTestResult ServiceWorkerPolicyTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&ServiceWorkerPolicyTestClient::CreateBrowser,
                                          CefRefPtr<ServiceWorkerPolicyTestClient>(this)))) {
        ServiceWorkerPolicyTestResult failure;
        failure.error = "Could not post service worker test browser creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
        std::fputs("Service worker policy test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> ServiceWorkerPolicyTestClient::GetLifeSpanHandler() {
    return this;
}

CefRefPtr<CefRequestHandler> ServiceWorkerPolicyTestClient::GetRequestHandler() {
    return this;
}

CefRefPtr<CefLoadHandler> ServiceWorkerPolicyTestClient::GetLoadHandler() {
    return this;
}

CefRefPtr<CefDisplayHandler> ServiceWorkerPolicyTestClient::GetDisplayHandler() {
    return this;
}

void ServiceWorkerPolicyTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP service worker policy test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowserSync(window, this, startupUrl_, settings, nullptr, nullptr)) {
        result_.error = "CEF rejected service worker test browser creation.";
        Complete();
    }
}

void ServiceWorkerPolicyTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool ServiceWorkerPolicyTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || !request) {
        result_.error = "Missing navigation data in service worker test.";
        RequestClose(browser);
        return true;
    }
    const std::string url = request->GetURL().ToString();
    const bool target = phase_ == Phase::Target && url == targetUrl_;
    if (!target && !(phase_ == Phase::Startup && url == startupUrl_)) {
        result_.error = "Unexpected navigation in service worker test.";
        RequestClose(browser);
        return true;
    }
    const bool cancel = handler_->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    if (target) {
        result_.targetCallbackObserved = true;
        result_.targetBlocked = cancel;
        if (cancel) {
            BeginCleanup(browser);
        }
    } else if (cancel) {
        result_.error = "Service worker startup navigation was blocked.";
        RequestClose(browser);
    }
    // La prueba observa la decisión real; una autorización incorrecta puede cargar el destino
    // local y debe detectarse mediante el documento mostrado, aunque no llegue otra petición HTTP.
    return cancel;
}

void ServiceWorkerPolicyTestClient::OnLoadEnd(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain()) {
        return;
    }
    const std::string url = frame->GetURL().ToString();
    const bool startup = phase_ == Phase::Startup && url == startupUrl_;
    const bool target = phase_ == Phase::Target && url == targetUrl_;
    if (!startup && !target) {
        return;
    }
    if (httpStatusCode != 200) {
        result_.error = "Service worker test document returned HTTP "
            + std::to_string(httpStatusCode) + ".";
        RequestClose(browser);
        return;
    }
    if (startup) {
        result_.startupLoaded = true;
        MaybeNavigateTarget(browser);
    } else {
        result_.targetLoaded = true;
        result_.targetStatusCode = httpStatusCode;
    }
}

void ServiceWorkerPolicyTestClient::MaybeNavigateTarget(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ != Phase::Startup || !result_.startupLoaded || !result_.workerReady) {
        return;
    }
    phase_ = Phase::PreparingTarget;
    if (!CefPostTask(TID_UI, base::BindOnce(&ServiceWorkerPolicyTestClient::PublishPolicyAndNavigate,
                                          CefRefPtr<ServiceWorkerPolicyTestClient>(this), browser))) {
        result_.error = "Could not post policy update for service worker test.";
        RequestClose(browser);
    }
}

void ServiceWorkerPolicyTestClient::PublishPolicyAndNavigate(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    CefURLParts parts;
    if (!CefParseURL(targetUrl_, parts)) {
        result_.error = "Could not parse service worker navigation target.";
        RequestClose(browser);
        return;
    }
    std::vector<std::string> rules;
    if (blockTarget_) {
        rules.push_back(CefString(&parts.host).ToString());
    }
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot updatedPolicy(
        2, context_.CompanyId(), "Updated service worker policy",
        UrlFilteringConfig(true, FilterMode::Denylist, std::move(rules)),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    result_.updatedPolicyPublished = manager_->PublishPolicy(context_, updatedPolicy)
        == PolicyPublicationResult::Published;
    if (!result_.updatedPolicyPublished) {
        result_.error = "Could not publish updated service worker policy.";
        RequestClose(browser);
        return;
    }
    phase_ = Phase::Target;
    result_.targetRequested = true;
    browser->GetMainFrame()->LoadURL(targetUrl_);
}

void ServiceWorkerPolicyTestClient::OnLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool isLoading, bool /*canGoBack*/, bool /*canGoForward*/) {
    CEF_REQUIRE_UI_THREAD();
    if (isLoading || phase_ != Phase::Target || !result_.targetLoaded || inspectionRequested_) {
        return;
    }
    inspectionRequested_ = true;
    // El título marcado solo lo produce el worker al recuperar la entrada de Cache Storage.
    browser->GetMainFrame()->ExecuteJavaScript(
        "document.title='SBP SW probe/'"
        "+(navigator.serviceWorker.controller && document.title==='SBP SW cached target'"
        "?'cache':'unexpected');", targetUrl_, 0);
}

void ServiceWorkerPolicyTestClient::OnTitleChange(
    CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    const std::string value = title.ToString();
    if (phase_ == Phase::Startup && value == "SBP SW ready") {
        result_.workerReady = true;
        MaybeNavigateTarget(browser);
    } else if (phase_ == Phase::Startup && value.starts_with("SBP SW startup error/")) {
        result_.error = value;
        RequestClose(browser);
    } else if (phase_ == Phase::Target && value == "SBP SW probe/cache") {
        result_.servedFromWorkerCache = true;
        BeginCleanup(browser);
    } else if (phase_ == Phase::Target && value == "SBP SW probe/unexpected") {
        result_.error = "Target was not the controlled service worker cache response.";
        BeginCleanup(browser);
    } else if (phase_ == Phase::Cleanup && value == "SBP SW cleanup/done") {
        result_.cleanupCompleted = true;
        RequestClose(browser);
    } else if (phase_ == Phase::Cleanup && value.starts_with("SBP SW cleanup/error/")) {
        if (result_.error.empty()) {
            result_.error = value;
        }
        RequestClose(browser);
    }
}

void ServiceWorkerPolicyTestClient::BeginCleanup(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Cleanup || phase_ == Phase::Closing || phase_ == Phase::Finished) {
        return;
    }
    phase_ = Phase::Cleanup;
    if (!CefPostTask(TID_UI, base::BindOnce(&ServiceWorkerPolicyTestClient::CleanupWorker,
                                          CefRefPtr<ServiceWorkerPolicyTestClient>(this), browser))) {
        result_.error = "Could not post service worker fixture cleanup.";
        RequestClose(browser);
    }
}

void ServiceWorkerPolicyTestClient::CleanupWorker(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    const auto frame = browser->GetMainFrame();
    // Retira el registro y la caché de este origen de prueba antes del cierre normal.
    frame->ExecuteJavaScript(R"SWCLEANUP((async function() {
    const timer = setTimeout(function() {
        document.title = 'SBP SW cleanup/error/TimeoutError';
    }, 10000);
    try {
        const registration = await navigator.serviceWorker.getRegistration('/');
        const removed = registration && await registration.unregister();
        const cleared = await caches.delete('sbp-navigation-policy-v1');
        clearTimeout(timer);
        document.title = removed && cleared ? 'SBP SW cleanup/done' : 'SBP SW cleanup/error/MissingFixture';
    } catch (error) {
        clearTimeout(timer);
        document.title = 'SBP SW cleanup/error/' + error.name;
    }
})();)SWCLEANUP", frame->GetURL(), 0);
}

void ServiceWorkerPolicyTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
    const CefString& /*errorText*/, const CefString& /*failedUrl*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || phase_ == Phase::Closing || phase_ == Phase::Finished
        || (errorCode == ERR_ABORTED && phase_ == Phase::Cleanup)) {
        return;
    }
    result_.error = "Service worker test navigation failed with CEF error "
        + std::to_string(static_cast<int>(errorCode)) + ".";
    RequestClose(browser);
}

void ServiceWorkerPolicyTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Closing || phase_ == Phase::Finished) {
        return;
    }
    phase_ = Phase::Closing;
    if (!CefPostTask(TID_UI, base::BindOnce(&ServiceWorkerPolicyTestClient::CloseBrowser,
                                          CefRefPtr<ServiceWorkerPolicyTestClient>(this), browser))) {
        result_.error = "Could not post service worker test browser closure.";
        CloseBrowser(browser);
    }
}

void ServiceWorkerPolicyTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void ServiceWorkerPolicyTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&ServiceWorkerPolicyTestClient::Complete,
                                          CefRefPtr<ServiceWorkerPolicyTestClient>(this)))) {
        std::fputs("Could not post service worker test completion.\n", stderr);
        std::abort();
    }
}

void ServiceWorkerPolicyTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    phase_ = Phase::Finished;
    completion_.set_value(result_);
}

}  // namespace sbp
