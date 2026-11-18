#pragma once

#include "browser/browser_client.h"
#include "browser/navigation_decision_notice.h"
#include "security/policy_manager.h"
#include "security/security_context.h"

#include <future>
#include <memory>
#include <string>
#include <vector>

namespace sbp {

enum class DocumentPolicyTestAction { Revalidate, Back, Forward, RevisitAfterRevocation };

struct DocumentPolicyTestResult {
    bool browserCreated = false;
    bool browserClosed = false;
    bool initialDocumentLoaded = false;
    bool sameDocumentEntryCreated = false;
    bool entryCreatedWithUserActivation = false;
    bool updatedPolicyPublished = false;
    bool actionRequested = false;
    bool neutralDocumentLoaded = false;
    bool revokedHistoryBlocked = false;
    bool documentInspected = false;
    bool originalDocumentPreserved = false;
    int documentStartNoticesAtUpdate = 0;
    int documentStartNotices = 0;
    std::vector<NavigationDecisionNotice> notices;
    std::string error;
};

// Observa un navegador local y delega la seguridad, los diálogos y el historial en BrowserClient real.
class DocumentPolicyTestClient final : public CefClient,
                                       public CefRequestHandler,
                                       public CefLoadHandler,
                                       public CefDisplayHandler {
public:
    DocumentPolicyTestClient(std::shared_ptr<PolicyManager> manager, SecurityContext context,
                             std::string initialUrl, std::string entryUrl,
                             DocumentPolicyTestAction action, bool restricted, bool useJavaScript);
    [[nodiscard]] DocumentPolicyTestResult Run();

    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;
    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     TransitionType transitionType) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int status) override;
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading,
                               bool canGoBack, bool canGoForward) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText, const CefString& failedUrl) override;
    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;
    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString& title) override;

private:
    ~DocumentPolicyTestClient() override = default;
    enum class Phase { Initial, CreatingEntry, PreparingForward, Publishing, AwaitingResult,
                       Revisiting, Probing, Closing, Finished };

    void CreateBrowser();
    void Advance(CefRefPtr<CefBrowser> browser);
    void OnEntryReady(CefRefPtr<CefBrowser> browser);
    void PostAdvance(CefRefPtr<CefBrowser> browser);
    void ProbeDocument(CefRefPtr<CefBrowser> browser);
    void CheckDeadline(CefRefPtr<CefBrowser> browser);
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void Complete();

    const std::shared_ptr<PolicyManager> manager_;
    const SecurityContext context_;
    const std::string initialUrl_;
    const std::string entryUrl_;
    const DocumentPolicyTestAction action_;
    const bool restricted_;
    const bool useJavaScript_;
    const std::shared_ptr<std::vector<NavigationDecisionNotice>> notices_;
    CefRefPtr<BrowserClient> client_;
    // Las fases y el resultado solo cambian en UI CEF; Run espera la promesa en el hilo de pruebas.
    Phase phase_ = Phase::Initial;
    DocumentPolicyTestResult result_;
    std::promise<DocumentPolicyTestResult> completion_;
    IMPLEMENT_REFCOUNTING(DocumentPolicyTestClient);
};

}  // namespace sbp
