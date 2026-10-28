#include "browser_client.h"

#include "include/wrapper/cef_helpers.h"

#include <cstdio>
#include <utility>

namespace sbp {

BrowserClient::BrowserClient(std::function<void(int)> on_browser_created,
                             std::function<void()> on_browser_closed)
    : on_browser_created_(std::move(on_browser_created)),
      on_browser_closed_(std::move(on_browser_closed)) {}

BrowserClient::~BrowserClient() = default;

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() {
    // CEF recibe una referencia al mismo cliente, visto como receptor del ciclo de vida.
    return this;
}

void BrowserClient::RequestClose() {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "CEF client close request received; CEF UI thread: %d.\n",
                 CefCurrentlyOn(TID_UI));
    // Todavía no existe un CefBrowser: confirmamos ese caso mediante la función registrada.
    // No llamamos manualmente a OnBeforeClose; ese aviso pertenece exclusivamente a CEF.
    if (on_browser_closed_) {
        on_browser_closed_();
    }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    const int browser_id = browser->GetIdentifier();
    std::fprintf(stderr, "CEF browser created: id %d.\n", browser_id);
    // Solo entregamos el identificador; no conservamos el navegador ni lo pasamos a la cola Qt.
    if (on_browser_created_) {
        on_browser_created_(browser_id);
    }
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "CEF browser closing: id %d.\n", browser->GetIdentifier());
    // Este aviso llega desde CEF; no solicita el cierre ni toca widgets Qt directamente.
    // La función recibida al construir el cliente encola la confirmación en el hilo Qt.
    if (on_browser_closed_) {
        on_browser_closed_();
    }
}

}  // namespace sbp
