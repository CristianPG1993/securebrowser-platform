#pragma once

#include "browser/security_request_handler.h"
#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include <future>
#include <string>

namespace sbp {

enum class NavigationTestAction { Direct, Link, Script, FormGet, FormPost, Blob };

struct NavigationActionTestResult {
    bool browserCreated = false;
    bool browserClosed = false;
    bool parentLoaded = false;
    int sourceLoads = 0;
    bool targetLoaded = false;
    int targetCallbacks = 0;
    int blockedAttempts = 0;
    int targetStatus = 0;
    std::string error;
};

// Ejecuta la acción desde un documento local y conserva la decisión real del handler.
class NavigationActionTestClient final : public CefClient,
                                        public CefLifeSpanHandler,
                                        public CefRequestHandler,
                                        public CefLoadHandler {
public:
    NavigationActionTestClient(CefRefPtr<SecurityRequestHandler> handler, std::string parentUrl,
                               std::string targetUrl, NavigationTestAction action, int attempts = 1);
    [[nodiscard]] NavigationActionTestResult Run();
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                     const CefString& errorText, const CefString& failedUrl) override;

private:
    ~NavigationActionTestClient() override = default;
    void CreateBrowser();
    void LaunchAction(CefRefPtr<CefBrowser> browser);
    void PostAction(CefRefPtr<CefBrowser> browser);
    [[nodiscard]] bool IsTarget(const CefString& url) const;
    [[nodiscard]] std::string RetrySourceUrl() const;
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();

    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string parentUrl_;
    const std::string targetUrl_;
    const NavigationTestAction action_;
    const int attempts_;
    // El estado y los resultados pertenecen a UI; el hilo de pruebas espera la promesa.
    bool started_ = false;
    bool retryPending_ = false;
    bool closing_ = false;
    NavigationActionTestResult result_;
    std::promise<NavigationActionTestResult> completion_;
    IMPLEMENT_REFCOUNTING(NavigationActionTestClient);
};

}  // namespace sbp
