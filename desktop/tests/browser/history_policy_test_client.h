#pragma once

#include "browser/security_request_handler.h"

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include <future>
#include <memory>
#include <string>

namespace sbp {

// Distingue carga inicial, cambio de política, decisión al volver y restauración efectiva.
struct HistoryPolicyTestResult {
    bool browserCreated = false;
    bool firstPageLoaded = false;
    bool secondPageLoaded = false;
    int firstPageStatusCode = 0;
    int secondPageStatusCode = 0;
    // -1 indica que no se recibió OnLoadEnd al volver; BFCache notifica cero en CEF 156.
    int returnLoadStatus = -1;
    bool updatedPolicyPublished = false;
    bool backRequested = false;
    bool backCallbackObserved = false;
    bool backBlocked = false;
    bool firstPageReturned = false;
    bool restoredFromBfcache = false;
    bool restoredDocumentInspected = false;
    bool browserClosed = false;
    std::string error;
};

// Mantiene el mismo navegador al cargar A, cargar B, actualizar la política e intentar volver a A.
class HistoryPolicyTestClient final : public CefClient,
                                      public CefLifeSpanHandler,
                                      public CefRequestHandler,
                                      public CefLoadHandler,
                                      public CefDisplayHandler {
public:
    HistoryPolicyTestClient(std::shared_ptr<PolicyManager> manager, SecurityContext context,
                            std::string firstUrl, std::string secondUrl,
                            bool useJavaScript, bool blockFirstPage);
    [[nodiscard]] HistoryPolicyTestResult Run();

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture,
                        bool isRedirect) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                               bool canGoBack, bool canGoForward) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

private:
    ~HistoryPolicyTestClient() override = default;

    enum class Phase { FirstPage, SecondPage, UpdatingPolicy, Returning, Closing, Finished };

    // El estado de fases y los resultados se modifican exclusivamente en el hilo UI de CEF.
    void CreateBrowser();
    void LoadSecondPage(CefRefPtr<CefBrowser> browser);
    void UpdatePolicyAndGoBack(CefRefPtr<CefBrowser> browser);
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();

    const std::shared_ptr<PolicyManager> manager_;
    const SecurityContext context_;
    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string firstUrl_;
    const std::string secondUrl_;
    const bool useJavaScript_;
    const bool blockFirstPage_;
    Phase phase_ = Phase::FirstPage;
    bool inspectionRequested_ = false;
    HistoryPolicyTestResult result_;
    std::promise<HistoryPolicyTestResult> completion_;

    IMPLEMENT_REFCOUNTING(HistoryPolicyTestClient);
};

}  // namespace sbp
