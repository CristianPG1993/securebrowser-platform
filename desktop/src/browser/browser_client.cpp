#include "browser_client.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
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

void BrowserClient::CreateBrowser(CefWindowInfo window_info) {
    CEF_REQUIRE_UI_THREAD();
    // Si Qt pidió cerrar antes de ejecutar esta tarea, ya no se debe crear una ventana hija.
    if (close_requested_ || creation_pending_ || browser_id_ != -1) {
        return;
    }
    creation_pending_ = true;
    CefBrowserSettings settings;
    // CEF copia los parámetros y conserva su referencia al cliente mientras crea el navegador.
    const bool accepted = CefBrowserHost::CreateBrowser(
        window_info, this, "https://example.com/", settings, nullptr, nullptr);
    std::fprintf(stderr, "CEF browser creation requested; accepted: %d; CEF UI thread: %d.\n",
                 accepted, CefCurrentlyOn(TID_UI));
    if (!accepted) {
        creation_pending_ = false;
        std::fputs("CEF browser creation failed: request rejected.\n", stderr);
        NotifyClosed();
    }
}

void BrowserClient::RequestClose() {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "CEF client close request received; CEF UI thread: %d.\n",
                 CefCurrentlyOn(TID_UI));
    close_requested_ = true;
    if (creation_pending_) {
        // No destruimos el anfitrión mientras CEF todavía puede crear su ventana hija.
        std::fputs("CEF close deferred until browser creation completes.\n", stderr);
        return;
    }
    if (browser_id_ != -1) {
        // CEF localiza el navegador por su identificador y nos entrega una referencia temporal.
        // Esta variable se libera al salir del bloque; el cliente no conserva el objeto navegador.
        const CefRefPtr<CefBrowser> browser = CefBrowserHost::GetBrowserByIdentifier(browser_id_);
        if (!browser) {
            // No confirmamos un cierre que CEF todavía no ha notificado mediante OnBeforeClose.
            std::fputs("CEF close request failed: registered browser unavailable.\n", stderr);
            return;
        }
        // false permite ejecutar los avisos JavaScript de salida; la petición no equivale al cierre.
        browser->GetHost()->CloseBrowser(false);
        return;
    }

    // Sin navegador ni creación pendiente ya podemos confirmar el cierre.
    NotifyClosed();
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    const int browser_id = browser->GetIdentifier();
    // Registramos la instancia para poder localizarla cuando Qt solicite su cierre.
    browser_id_ = browser_id;
    creation_pending_ = false;
    std::fprintf(stderr, "CEF browser created: id %d.\n", browser_id);
    // Solo entregamos el identificador; no conservamos el navegador ni lo pasamos a la cola Qt.
    if (on_browser_created_) {
        on_browser_created_(browser_id);
    }
    if (close_requested_) {
        // Esperamos a que termine este callback de creación antes de solicitar el cierre real.
        if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::RequestClose,
                                              CefRefPtr<BrowserClient>(this)))) {
            std::fputs("CEF deferred close request could not be posted.\n", stderr);
        }
    }
}

bool BrowserClient::DoClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    // Alloy envía por defecto WM_CLOSE a la ventana raíz Qt, cuyo cierre seguimos reteniendo.
    // Publicamos también WM_CLOSE para la hija CEF. Al procesarlo, CEF ya habrá marcado su cierre
    // como aceptado y su manejador nativo podrá destruirla sin destruir el anfitrión Qt.
    const HWND child_window = browser->GetHost()->GetWindowHandle();
    if (child_window == nullptr || !PostMessage(child_window, WM_CLOSE, 0, 0)) {
        std::fputs("CEF native child close message could not be posted.\n", stderr);
    }
    // false permite a CEF completar su transición interna al estado de cierre aceptado.
    return false;
}

bool BrowserClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                                 int, const CefString&, const CefString&,
                                 WindowOpenDisposition, bool, const CefPopupFeatures&,
                                 CefWindowInfo&, CefRefPtr<CefClient>&, CefBrowserSettings&,
                                 CefRefPtr<CefDictionaryValue>&, bool*) {
    CEF_REQUIRE_UI_THREAD();
    // Evita asociar un segundo navegador al cliente que gestiona esta única ventana.
    return true;
}

void BrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    std::fprintf(stderr, "CEF browser closing: id %d.\n", browser->GetIdentifier());
    // Retiramos el identificador antes de avisar a Qt; ya no se solicitarán acciones a esta instancia.
    browser_id_ = -1;
    NotifyClosed();
}

void BrowserClient::NotifyClosed() {
    CEF_REQUIRE_UI_THREAD();
    if (close_notified_) {
        return;
    }
    close_notified_ = true;
    // La tarea solo conserva la función: ninguna referencia al navegador sale del callback.
    // Se ejecutará después de regresar de OnBeforeClose y liberarse sus referencias temporales.
    if (!CefPostTask(TID_UI, base::BindOnce([](std::function<void()> on_closed) {
            if (on_closed) {
                on_closed();
            }
        }, on_browser_closed_))) {
        std::fputs("CEF final close notification could not be posted.\n", stderr);
    }
}

}  // namespace sbp
