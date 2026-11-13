#include "navigation_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_request.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <utility>

namespace sbp {

NavigationTestClient::NavigationTestClient(CefRefPtr<SecurityRequestHandler> handler,
                                         std::string url, bool expectCancellation)
    : handler_(std::move(handler)), url_(std::move(url)),
      expectCancellation_(expectCancellation) {}

NavigationTestResult NavigationTestClient::Run() {
    auto completion = completion_.get_future();
    // BindOnce retiene el cliente hasta ejecutar la creación en el hilo UI.
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationTestClient::CreateBrowser,
                                          CefRefPtr<NavigationTestClient>(this)))) {
        NavigationTestResult failure;
        failure.error = "Could not post browser creation to the CEF UI thread.";
        return failure;
    }
    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        // No se permite continuar hacia CefShutdown con un navegador de prueba sin cerrar.
        // La salida anormal hace fallar CTest y conserva el perfil para el diagnóstico.
        std::fputs("CEF navigation test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

CefRefPtr<CefLifeSpanHandler> NavigationTestClient::GetLifeSpanHandler() {
    return this;
}

CefRefPtr<CefRequestHandler> NavigationTestClient::GetRequestHandler() {
    return this;
}

CefRefPtr<CefLoadHandler> NavigationTestClient::GetLoadHandler() {
    return this;
}

void NavigationTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP navigation test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);
    const CefBrowserSettings settings;
    const auto browser = CefBrowserHost::CreateBrowserSync(
        window, this, url_, settings, nullptr, nullptr);
    if (!browser) {
        result_.error = "CEF rejected browser creation.";
        Complete();
    }
}

void NavigationTestClient::OnAfterCreated(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserCreated = true;
}

bool NavigationTestClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                         CefRefPtr<CefFrame> frame,
                                         CefRefPtr<CefRequest> request,
                                         bool userGesture, bool isRedirect) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || !request || request->GetURL().ToString() != url_) {
        result_.error = "An unexpected navigation reached the test client.";
        RequestClose(browser);
        return true;
    }
    result_.callbackObserved = true;
    result_.cancelledBySecurity = handler_->OnBeforeBrowse(
        browser, frame, request, userGesture, isRedirect);

    // Un permiso inesperado hace fallar la aserción; la prueba lo cancela para no salir a Internet.
    // Los destinos permitidos continúan hasta OnLoadEnd.
    if (result_.cancelledBySecurity || expectCancellation_) {
        RequestClose(browser);
        return true;
    }
    return false;
}

void NavigationTestClient::OnLoadEnd(CefRefPtr<CefBrowser> browser,
                                    CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain()) {
        return;
    }
    if (frame->GetURL().ToString() == url_) {
        result_.documentLoaded = true;
        result_.documentStatusCode = httpStatusCode;
        // Chromium puede cargar about:blank sin ejecutar el interceptor de OnBeforeBrowse.
        // Se comprueba el permiso mediante una llamada directa, con navegador y frame reales
        // en el hilo UI. No se registra como un callback emitido por CEF ni como bloqueo efectivo.
        if (!result_.callbackObserved && url_ == "about:blank") {
            const auto request = CefRequest::Create();
            request->SetURL(url_);
            result_.cancelledBySecurity = handler_->OnBeforeBrowse(
                browser, frame, request, false, false);
            result_.handlerCheckedDirectly = true;
        }
        RequestClose(browser);
    }
}

void NavigationTestClient::OnLoadError(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame, ErrorCode errorCode,
                                      const CefString& /*errorText*/,
                                      const CefString& failedUrl) {
    CEF_REQUIRE_UI_THREAD();
    if (!frame || !frame->IsMain() || failedUrl.ToString() != url_
        || (errorCode == ERR_ABORTED && closing_)) {
        return;
    }
    result_.error = "Navigation failed with CEF error "
        + std::to_string(static_cast<int>(errorCode)) + ".";
    RequestClose(browser);
}

void NavigationTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    closing_ = true;
    // Se cierra fuera del callback de navegación, conservando cliente y navegador hasta la tarea.
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationTestClient::CloseBrowser,
                                          CefRefPtr<NavigationTestClient>(this), browser))) {
        result_.error = "Could not post browser closure.";
        CloseBrowser(browser);
    }
}

void NavigationTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser->GetHost()->CloseBrowser(true);
}

void NavigationTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    result_.browserClosed = true;
    // La tarea de finalización se ejecuta después de retornar del último callback de cierre.
    if (!CefPostTask(TID_UI, base::BindOnce(&NavigationTestClient::Complete,
                                          CefRefPtr<NavigationTestClient>(this)))) {
        std::fputs("Could not post completion after browser closure.\n", stderr);
        std::abort();
    }
}

void NavigationTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    completion_.set_value(result_);
}

}  // namespace sbp
