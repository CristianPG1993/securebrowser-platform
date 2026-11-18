#include "redirect_navigation_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_parser.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sbp {

RedirectNavigationTestClient::RedirectNavigationTestClient(
    CefRefPtr<SecurityRequestHandler> handler, std::string port, int redirectStatus)
    : handler_(std::move(handler)),
      firstUrl_("http://start.sbp.test:" + port + "/redirect/"
                + std::to_string(redirectStatus) + "/first"),
      middleUrl_("http://middle.sbp.test:" + port + "/redirect/"
                 + std::to_string(redirectStatus) + "/second"),
      targetUrl_("http://target.sbp.test:" + port + "/redirect-target") {}

RedirectNavigationTestResult RedirectNavigationTestClient::Run() {
    auto completion = completion_.get_future();
    if (!CefPostTask(TID_UI, base::BindOnce(&RedirectNavigationTestClient::CreateBrowser,
                                          CefRefPtr<RedirectNavigationTestClient>(this)))) {
        RedirectNavigationTestResult failure;
        failure.error = "Could not post redirect test browser creation.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        std::fputs("Redirect navigation test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> RedirectNavigationTestClient::GetLifeSpanHandler() { return this; }
CefRefPtr<CefRequestHandler> RedirectNavigationTestClient::GetRequestHandler() { return this; }
CefRefPtr<CefLoadHandler> RedirectNavigationTestClient::GetLoadHandler() { return this; }

void RedirectNavigationTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP redirect navigation test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    if (!CefBrowserHost::CreateBrowserSync(window, this, firstUrl_, settings, nullptr, nullptr)) {
        result_.error = "CEF rejected redirect test browser creation.";
        Complete();
    }
}

void RedirectNavigationTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool RedirectNavigationTestClient::OnBeforeBrowse(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || !request) {
        result_.error = "Missing main-frame redirect navigation data.";
        RequestClose(browser);
        return true;
    }
    const std::string url = request->GetURL().ToString();
    if (url != firstUrl_ && url != middleUrl_ && url != targetUrl_) {
        result_.error = "Unexpected navigation in redirect test.";
        RequestClose(browser);
        return true;
    }
    ++result_.uiCallbacks;
    if (isRedirect) {
        ++result_.uiRedirectCallbacks;
    }
    const bool cancel = handler_->OnBeforeBrowse(browser, frame, request, userGesture, isRedirect);
    Trace(cancel ? "UI:block" : "UI:allow", request->GetURL());
    if (url == middleUrl_) {
        result_.middleCallbackObserved = true;
        result_.middleBlocked = cancel;
    }
    if (url == targetUrl_) {
        result_.targetCallbackObserved = true;
        result_.targetBlocked = cancel;
    }
    if (cancel) {
        if (url == firstUrl_) {
            result_.error = "An allowed redirect source was blocked.";
        }
        RequestClose(browser);
    }
    // El resultado esperado no interviene en la decisión ni evita una petición local incorrecta.
    return cancel;
}

CefRefPtr<CefResourceRequestHandler> RedirectNavigationTestClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
    bool isNavigation, bool isDownload, const CefString& requestInitiator,
    bool& disableDefaultHandling) {
    CEF_REQUIRE_IO_THREAD();
    auto delegate = handler_->GetResourceRequestHandler(browser, frame, request,
        isNavigation, isDownload, requestInitiator, disableDefaultHandling);
    if (!isNavigation || isDownload || !request) {
        return delegate;
    }
    delegates_[request->GetIdentifier()] = delegate;
    Trace("IO:get#" + std::to_string(request->GetIdentifier()), request->GetURL());
    return this;
}

CefResourceRequestHandler::ReturnValue RedirectNavigationTestClient::OnBeforeResourceLoad(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, CefRefPtr<CefCallback> callback) {
    CEF_REQUIRE_IO_THREAD();
    Trace("IO:load#" + std::to_string(request->GetIdentifier()), request->GetURL());
    {
        const std::lock_guard<std::mutex> lock(traceMutex_);
        ++ioLoads_;
    }
    const auto delegate = delegates_.at(request->GetIdentifier());
    return delegate ? delegate->OnBeforeResourceLoad(browser, frame, request, callback) : RV_CONTINUE;
}

void RedirectNavigationTestClient::OnResourceRedirect(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response, CefString& newUrl) {
    CEF_REQUIRE_IO_THREAD();
    const auto delegate = delegates_.at(request->GetIdentifier());
    if (delegate) {
        delegate->OnResourceRedirect(browser, frame, request, response, newUrl);
    }
    Trace("IO:redirect:" + std::to_string(response->GetStatus()) + "#"
        + std::to_string(request->GetIdentifier()), newUrl);
    const std::lock_guard<std::mutex> lock(traceMutex_);
    ++ioRedirects_;
}

void RedirectNavigationTestClient::OnResourceLoadComplete(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response,
    URLRequestStatus status, int64_t receivedContentLength) {
    CEF_REQUIRE_IO_THREAD();
    const auto delegate = delegates_.at(request->GetIdentifier());
    if (delegate) {
        delegate->OnResourceLoadComplete(browser, frame, request, response, status,
                                          receivedContentLength);
    }
    Trace("IO:complete:" + std::to_string(static_cast<int>(status)) + "#"
        + std::to_string(request->GetIdentifier()), request->GetURL());
    delegates_.erase(request->GetIdentifier());
}

void RedirectNavigationTestClient::OnLoadEnd(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_ && frame && frame->IsMain() && frame->GetURL().ToString() == targetUrl_) {
        result_.targetLoaded = true;
        result_.targetStatus = httpStatusCode;
        RequestClose(browser);
    }
}

void RedirectNavigationTestClient::OnLoadError(
    CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
    const CefString& /*errorText*/, const CefString& /*failedUrl*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!closing_ && frame && frame->IsMain()) {
        result_.error = "Redirect test navigation failed with CEF error "
            + std::to_string(static_cast<int>(errorCode)) + ".";
        RequestClose(browser);
    }
}

void RedirectNavigationTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    closing_ = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&RedirectNavigationTestClient::CloseBrowser,
                                          CefRefPtr<RedirectNavigationTestClient>(this), browser))) {
        result_.error = "Could not post redirect test browser closure.";
        CloseBrowser(browser);
    }
}

void RedirectNavigationTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void RedirectNavigationTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&RedirectNavigationTestClient::Complete,
                                          CefRefPtr<RedirectNavigationTestClient>(this)))) {
        std::fputs("Could not post redirect test completion.\n", stderr);
        std::abort();
    }
}

void RedirectNavigationTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    {
        const std::lock_guard<std::mutex> lock(traceMutex_);
        result_.trace = trace_;
        result_.ioLoads = ioLoads_;
        result_.ioRedirects = ioRedirects_;
    }
    completion_.set_value(result_);
}

void RedirectNavigationTestClient::Trace(const std::string& stage, const CefString& url) {
    CefURLParts parts;
    const std::string host = CefParseURL(url, parts)
        ? CefString(&parts.host).ToString() : "unknown";
    const std::lock_guard<std::mutex> lock(traceMutex_);
    trace_.push_back(stage + " " + host);
}

}  // namespace sbp
