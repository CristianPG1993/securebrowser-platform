#pragma once

#include "browser/security_request_handler.h"

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include <future>
#include <string>
#include <vector>

namespace sbp {

struct SubframeNavigationTestResult {
    bool browserCreated = false;
    bool browserClosed = false;
    bool parentCallbackObserved = false;
    bool parentLoaded = false;
    bool parentPreserved = false;
    bool childStarted = false;
    bool childCallbackObserved = false;
    bool internalHandlerCheckedDirectly = false;
    bool childBlocked = false;
    bool childLoaded = false;
    bool childLoadEndObserved = false;
    bool childDocumentVerified = false;
    int childStatus = 0;
    int childWebCallbacks = 0;
    int childRedirectCallbacks = 0;
    std::vector<std::string> trace;
    std::string error;
};

// Mantiene la página principal permitida mientras comprueba una navegación del iframe.
class SubframeNavigationTestClient final : public CefClient,
                                          public CefLifeSpanHandler,
                                          public CefRequestHandler,
                                          public CefLoadHandler,
                                          public CefDisplayHandler {
public:
    SubframeNavigationTestClient(CefRefPtr<SecurityRequestHandler> handler, std::string parentUrl,
                                 std::string childUrl, std::string expectedChildUrl,
                                 std::string expectedMarker);
    [[nodiscard]] SubframeNavigationTestResult Run();

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

private:
    ~SubframeNavigationTestClient() override = default;
    void CreateBrowser();
    void StartChild(CefRefPtr<CefBrowser> browser);
    void VerifyInternalChild(CefRefPtr<CefBrowser> browser);
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();
    // Serializa los argumentos del script como cadenas JSON, conservando sus escapes.
    [[nodiscard]] static std::string JavaScriptString(const std::string& value);
    void Trace(const std::string& stage, const CefString& url);

    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string parentUrl_;
    const std::string childUrl_;
    const std::string expectedChildUrl_;
    const std::string expectedMarker_;
    const bool internalChild_;
    // Estado exclusivo del hilo UI; GoogleTest solo recibe el resultado tras el cierre.
    bool closing_ = false;
    SubframeNavigationTestResult result_;
    std::promise<SubframeNavigationTestResult> completion_;

    IMPLEMENT_REFCOUNTING(SubframeNavigationTestClient);
};

}  // namespace sbp
