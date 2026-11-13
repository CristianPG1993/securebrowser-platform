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

// Registra por separado la preparación del worker, la decisión y la procedencia del documento.
struct ServiceWorkerPolicyTestResult {
    bool browserCreated = false;
    bool startupLoaded = false;
    bool workerReady = false;
    bool updatedPolicyPublished = false;
    bool targetRequested = false;
    bool targetCallbackObserved = false;
    bool targetBlocked = false;
    bool targetLoaded = false;
    bool servedFromWorkerCache = false;
    bool cleanupCompleted = false;
    bool browserClosed = false;
    int targetStatusCode = 0;
    std::string error;
};

// Instala un worker real, actualiza la política y navega a un documento ya preparado en su caché.
class ServiceWorkerPolicyTestClient final : public CefClient,
                                            public CefLifeSpanHandler,
                                            public CefRequestHandler,
                                            public CefLoadHandler,
                                            public CefDisplayHandler {
public:
    ServiceWorkerPolicyTestClient(std::shared_ptr<PolicyManager> manager, SecurityContext context,
                                  std::string origin, bool blockTarget);
    [[nodiscard]] ServiceWorkerPolicyTestResult Run();

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
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                               bool canGoBack, bool canGoForward) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

private:
    ~ServiceWorkerPolicyTestClient() override = default;
    enum class Phase { Startup, PreparingTarget, Target, Cleanup, Closing, Finished };

    // Fases y resultados pertenecen al hilo UI; el hilo de GoogleTest solo espera la entrega final.
    void CreateBrowser();
    void MaybeNavigateTarget(CefRefPtr<CefBrowser> browser);
    void PublishPolicyAndNavigate(CefRefPtr<CefBrowser> browser);
    void BeginCleanup(CefRefPtr<CefBrowser> browser);
    void CleanupWorker(CefRefPtr<CefBrowser> browser);
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();

    const std::shared_ptr<PolicyManager> manager_;
    const SecurityContext context_;
    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string startupUrl_;
    const std::string targetUrl_;
    const bool blockTarget_;
    Phase phase_ = Phase::Startup;
    bool inspectionRequested_ = false;
    ServiceWorkerPolicyTestResult result_;
    std::promise<ServiceWorkerPolicyTestResult> completion_;

    IMPLEMENT_REFCOUNTING(ServiceWorkerPolicyTestClient);
};

}  // namespace sbp
