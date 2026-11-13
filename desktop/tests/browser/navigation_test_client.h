#pragma once

#include "browser/security_request_handler.h"

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include <future>
#include <string>

namespace sbp {

// Datos entregados al hilo de GoogleTest una vez finalizado el cierre en CEF.
struct NavigationTestResult {
    bool browserCreated = false;
    bool callbackObserved = false;
    // Solo about:blank se comprueba directamente si Chromium omite OnBeforeBrowse.
    bool handlerCheckedDirectly = false;
    bool cancelledBySecurity = false;
    bool documentLoaded = false;
    bool browserClosed = false;
    std::string error;
};

// Observa una navegación principal real y delega la decisión en el handler de producción.
class NavigationTestClient final : public CefClient,
                                   public CefLifeSpanHandler,
                                   public CefRequestHandler,
                                   public CefLoadHandler {
public:
    // La URL y el resultado esperado pertenecen exclusivamente a esta prueba.
    NavigationTestClient(CefRefPtr<SecurityRequestHandler> handler,
                         std::string url, bool expectCancellation);

    // Se llama una sola vez desde GoogleTest; CEF trabaja en su propio hilo UI.
    [[nodiscard]] NavigationTestResult Run();

    // Este cliente de prueba observa el ciclo de vida, la petición y la carga.
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefRequestHandler> GetRequestHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;
    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request, bool userGesture,
                        bool isRedirect) override;
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;

private:
    ~NavigationTestClient() override = default;

    // Todos estos métodos y result_ se utilizan exclusivamente en el hilo UI de CEF.
    void CreateBrowser();
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();

    const CefRefPtr<SecurityRequestHandler> handler_;
    const std::string url_;
    const bool expectCancellation_;
    bool closing_ = false;
    NavigationTestResult result_;
    std::promise<NavigationTestResult> completion_;

    IMPLEMENT_REFCOUNTING(NavigationTestClient);
};

}  // namespace sbp
