#pragma once

#include "browser/security_request_handler.h"

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include "include/cef_resource_request_handler.h"

#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace sbp {

struct RedirectNavigationTestResult {
    bool browserCreated = false;
    bool browserClosed = false;
    bool middleCallbackObserved = false;
    bool middleBlocked = false;
    bool targetCallbackObserved = false;
    bool targetBlocked = false;
    bool targetLoaded = false;
    int targetStatus = 0;
    int uiCallbacks = 0;
    int uiRedirectCallbacks = 0;
    int ioLoads = 0;
    int ioRedirects = 0;
    std::vector<std::string> trace;
    std::string error;
};

// Observa una cadena real sin sustituir las decisiones del handler de producción.
class RedirectNavigationTestClient final : public CefClient,
                                          public CefLifeSpanHandler,
                                          public CefRequestHandler,
                                          public CefResourceRequestHandler,
                                          public CefLoadHandler {
public:
    RedirectNavigationTestClient(CefRefPtr<SecurityRequestHandler> handler,
                                 std::string port, int redirectStatus);
    [[nodiscard]] RedirectNavigationTestResult Run();

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;
    CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
        CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request,
        bool isNavigation, bool isDownload, const CefString& requestInitiator,
        bool& disableDefaultHandling) override;
    ReturnValue OnBeforeResourceLoad(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefRequest> request,
                                     CefRefPtr<CefCallback> callback) override;
    void OnResourceRedirect(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                            CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response,
                            CefString& newUrl) override;
    void OnResourceLoadComplete(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                CefRefPtr<CefRequest> request, CefRefPtr<CefResponse> response,
                                URLRequestStatus status, int64_t receivedContentLength) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;

private:
    ~RedirectNavigationTestClient() override = default;
    void CreateBrowser();
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();
    // Guarda únicamente etapa y host interpretado; nunca una URL completa.
    void Trace(const std::string& stage, const CefString& url);

    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string firstUrl_;
    const std::string middleUrl_;
    const std::string targetUrl_;
    // El resultado de navegación y el cierre solo se modifican en UI.
    bool closing_ = false;
    RedirectNavigationTestResult result_;
    std::promise<RedirectNavigationTestResult> completion_;
    // Los delegados por petición pertenecen exclusivamente al hilo IO.
    std::map<std::uint64_t, CefRefPtr<CefResourceRequestHandler>> delegates_;
    // La traza es compartida por UI e IO y se copia al resultado antes de entregarlo.
    std::mutex traceMutex_;
    std::vector<std::string> trace_;
    int ioLoads_ = 0;
    int ioRedirects_ = 0;

    IMPLEMENT_REFCOUNTING(RedirectNavigationTestClient);
};

}  // namespace sbp
