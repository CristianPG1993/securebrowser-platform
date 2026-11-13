#include "history_policy_test_client.h"

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

HistoryPolicyTestClient::HistoryPolicyTestClient(
    std::shared_ptr<PolicyManager> manager, SecurityContext context,
    std::string firstUrl, std::string secondUrl, bool useJavaScript, bool blockFirstPage)
    : manager_(std::move(manager)), context_(std::move(context)),
      handler_(new SecurityRequestHandler(manager_)), firstUrl_(std::move(firstUrl)),
      secondUrl_(std::move(secondUrl)), useJavaScript_(useJavaScript),
      blockFirstPage_(blockFirstPage) {}

HistoryPolicyTestResult HistoryPolicyTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&HistoryPolicyTestClient::CreateBrowser,
                                          CefRefPtr<HistoryPolicyTestClient>(this)))) {
        HistoryPolicyTestResult failure;
        failure.error = "Could not post history test browser creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("History policy test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> HistoryPolicyTestClient::GetLifeSpanHandler() {
    return this;
}

CefRefPtr<CefRequestHandler> HistoryPolicyTestClient::GetRequestHandler() {
    return this;
}

CefRefPtr<CefLoadHandler> HistoryPolicyTestClient::GetLoadHandler() {
    return this;
}

CefRefPtr<CefDisplayHandler> HistoryPolicyTestClient::GetDisplayHandler() {
    return this;
}

void HistoryPolicyTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP history policy test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowserSync(window, this, firstUrl_, settings, nullptr, nullptr)) {
        result_.error = "CEF rejected history test browser creation.";
        Complete();
    }
}

void HistoryPolicyTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool HistoryPolicyTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || !request) {
        result_.error = "Missing main-frame navigation data in history test.";
        RequestClose(browser);
        return true;
    }
    const std::string url = request->GetURL().ToString();
    const bool expected = (phase_ == Phase::FirstPage && url == firstUrl_)
        || (phase_ == Phase::SecondPage && url == secondUrl_)
        || (phase_ == Phase::Returning && url == firstUrl_);
    if (!expected) {
        result_.error = "Unexpected navigation in history test.";
        RequestClose(browser);
        return true;
    }
    const bool cancel = handler_->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    if (phase_ == Phase::Returning) {
        result_.backCallbackObserved = true;
        result_.backBlocked = cancel;
    } else if (cancel) {
        result_.error = "An initially permitted history test page was blocked.";
    }
    if (cancel) {
        RequestClose(browser);
    }
    // Se devuelve la decisión real: el cliente no impone el resultado esperado de la prueba.
    return cancel;
}

void HistoryPolicyTestClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain()) {
        return;
    }
    const std::string url = frame->GetURL().ToString();
    if (phase_ == Phase::Returning && url == firstUrl_) {
        // CEF 156 emite OnLoadEnd(..., 0) para BFCache. El retorno se comprueba mediante
        // la dirección y el documento activo, sin confundirlo con una nueva respuesta HTTP.
        result_.returnLoadStatus = httpStatusCode;
        return;
    }
    if (phase_ != Phase::FirstPage && phase_ != Phase::SecondPage) {
        return;
    }
    if (phase_ == Phase::FirstPage && url == "about:blank") {
        return;
    }
    const bool firstPage = phase_ == Phase::FirstPage;
    if (url != (firstPage ? firstUrl_ : secondUrl_)) {
        result_.error = "Unexpected initial document in history test.";
        RequestClose(browser);
        return;
    }
    if (firstPage) {
        result_.firstPageStatusCode = httpStatusCode;
    } else {
        result_.secondPageStatusCode = httpStatusCode;
    }
    if (httpStatusCode != 200) {
        result_.error = std::string(firstPage ? "First" : "Second")
            + " history test page returned HTTP " + std::to_string(httpStatusCode) + ".";
        RequestClose(browser);
        return;
    }
    if (firstPage) {
        result_.firstPageLoaded = true;
    } else {
        result_.secondPageLoaded = true;
    }
}

void HistoryPolicyTestClient::OnLoadingStateChange(
    CefRefPtr<CefBrowser> browser, bool isLoading,
    bool /*canGoBack*/, bool /*canGoForward*/) {
    CEF_REQUIRE_UI_THREAD();
    if (isLoading || phase_ == Phase::Closing || phase_ == Phase::Finished) {
        return;
    }
    const auto frame = browser->GetMainFrame();
    const std::string url = frame->GetURL().ToString();
    if (phase_ == Phase::FirstPage && result_.firstPageLoaded && url == firstUrl_) {
        phase_ = Phase::SecondPage;
        if (!CefPostTask(TID_UI, base::BindOnce(&HistoryPolicyTestClient::LoadSecondPage,
                                              CefRefPtr<HistoryPolicyTestClient>(this), browser))) {
            result_.error = "Could not post the second history navigation.";
            RequestClose(browser);
        }
    } else if (phase_ == Phase::SecondPage && result_.secondPageLoaded && url == secondUrl_) {
        phase_ = Phase::UpdatingPolicy;
        if (!CefPostTask(TID_UI, base::BindOnce(&HistoryPolicyTestClient::UpdatePolicyAndGoBack,
                                              CefRefPtr<HistoryPolicyTestClient>(this), browser))) {
            result_.error = "Could not post policy update and back navigation.";
            RequestClose(browser);
        }
    } else if (phase_ == Phase::Returning && url == firstUrl_ && !inspectionRequested_) {
        result_.firstPageReturned = true;
        inspectionRequested_ = true;
        // La consulta se ejecuta en el documento activo. pageshow.persisted lo rellena la página,
        // no el cliente; un prefijo propio permite ignorar los avisos de título ordinarios.
        frame->ExecuteJavaScript(
            "document.title='SBP history probe/'"
            "+(window.sbpHistoryRestored?'bfcache':'normal');", firstUrl_, 0);
    }
}

void HistoryPolicyTestClient::LoadSecondPage(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetMainFrame()->LoadURL(secondUrl_);
}

void HistoryPolicyTestClient::UpdatePolicyAndGoBack(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (!browser->CanGoBack()) {
        result_.error = "The browser did not retain the first history entry.";
        RequestClose(browser);
        return;
    }
    CefURLParts parts;
    if (!CefParseURL(firstUrl_, parts)) {
        result_.error = "Could not parse the first history test URL.";
        RequestClose(browser);
        return;
    }
    std::vector<std::string> rules;
    if (blockFirstPage_) {
        rules.push_back(CefString(&parts.host).ToString());
    }
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot updatedPolicy(
        2, context_.CompanyId(), "Updated history policy",
        UrlFilteringConfig(true, FilterMode::Denylist, std::move(rules)),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    result_.updatedPolicyPublished = manager_->PublishPolicy(context_, updatedPolicy)
        == PolicyPublicationResult::Published;
    if (!result_.updatedPolicyPublished) {
        result_.error = "Could not publish the updated history policy.";
        RequestClose(browser);
        return;
    }
    phase_ = Phase::Returning;
    result_.backRequested = true;
    if (useJavaScript_) {
        browser->GetMainFrame()->ExecuteJavaScript("history.back();", secondUrl_, 0);
    } else {
        browser->GoBack();
    }
}

void HistoryPolicyTestClient::OnAddressChange(CefRefPtr<CefBrowser> /*browser*/,
                                            CefRefPtr<CefFrame> frame, const CefString& url) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Returning && frame && frame->IsMain() && url.ToString() == firstUrl_) {
        // Una dirección restaurada cuenta como retorno aunque no haya otra petición HTTP.
        result_.firstPageReturned = true;
    }
}

void HistoryPolicyTestClient::OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ != Phase::Returning || !inspectionRequested_) {
        return;
    }
    const std::string value = title.ToString();
    if (value == "SBP history probe/bfcache" || value == "SBP history probe/normal") {
        result_.restoredDocumentInspected = true;
        result_.restoredFromBfcache = value == "SBP history probe/bfcache";
        RequestClose(browser);
    }
}

void HistoryPolicyTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
    const CefString& /*errorText*/, const CefString& /*failedUrl*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || (errorCode == ERR_ABORTED && phase_ == Phase::Closing)) {
        return;
    }
    result_.error = "History test navigation failed with CEF error "
        + std::to_string(static_cast<int>(errorCode)) + ".";
    RequestClose(browser);
}

void HistoryPolicyTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (phase_ == Phase::Closing || phase_ == Phase::Finished) {
        return;
    }
    phase_ = Phase::Closing;
    if (!CefPostTask(TID_UI, base::BindOnce(&HistoryPolicyTestClient::CloseBrowser,
                                          CefRefPtr<HistoryPolicyTestClient>(this), browser))) {
        result_.error = "Could not post history test browser closure.";
        CloseBrowser(browser);
    }
}

void HistoryPolicyTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void HistoryPolicyTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&HistoryPolicyTestClient::Complete,
                                          CefRefPtr<HistoryPolicyTestClient>(this)))) {
        std::fputs("Could not post history test completion.\n", stderr);
        std::abort();
    }
}

void HistoryPolicyTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    phase_ = Phase::Finished;
    completion_.set_value(result_);
}

}  // namespace sbp
