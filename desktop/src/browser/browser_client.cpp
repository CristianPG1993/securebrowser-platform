#include "browser_client.h"

#include "browser/browser_history_visitor.h"

#include "include/base/cef_callback.h"
#include "include/cef_frame.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>
#include <utility>

namespace sbp {

BrowserClient::BrowserClient(BrowserClientCallbacks callbacks)
    : callbacks_(std::move(callbacks)) {}

BrowserClient::~BrowserClient() = default;

CefRefPtr<CefLifeSpanHandler> BrowserClient::GetLifeSpanHandler() {
    // CEF recibe una referencia al mismo cliente, visto como receptor del ciclo de vida.
    return this;
}

CefRefPtr<CefDisplayHandler> BrowserClient::GetDisplayHandler() {
    // Devolvemos una referencia al cliente existente, visto como manejador de presentación.
    return this;
}

CefRefPtr<CefLoadHandler> BrowserClient::GetLoadHandler() {
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

void BrowserClient::Navigate(std::string url) {
    CEF_REQUIRE_UI_THREAD();
    // La tarea puede llegar antes de la creación o después de que se haya solicitado cerrar.
    if (browser_id_ == -1 || close_requested_) {
        std::fputs("CEF navigation skipped: browser unavailable or closing.\n", stderr);
        return;
    }
    // Obtenemos una referencia temporal; ningún objeto navegador se comparte con el hilo Qt.
    const CefRefPtr<CefBrowser> browser = CefBrowserHost::GetBrowserByIdentifier(browser_id_);
    if (!browser) {
        std::fputs("CEF navigation skipped: registered browser unavailable.\n", stderr);
        return;
    }
    // El frame principal representa el documento de la página, frente a sus posibles subframes.
    const CefRefPtr<CefFrame> frame = browser->GetMainFrame();
    if (!frame) {
        std::fputs("CEF navigation skipped: main frame unavailable.\n", stderr);
        return;
    }
    // LoadURL solicita la carga asíncrona; CEF mantiene sus comprobaciones normales de certificados.
    frame->LoadURL(CefString(url));
    std::fprintf(stderr, "CEF navigation requested; CEF UI thread: %d.\n", CefCurrentlyOn(TID_UI));
}

void BrowserClient::GoBack() {
    CEF_REQUIRE_UI_THREAD();
    // Una tarea tardía no debe actuar sobre un navegador inexistente o que ya se está cerrando.
    if (browser_id_ == -1 || close_requested_) {
        return;
    }
    // Consultamos la instancia registrada sin conservar una referencia permanente al navegador.
    const CefRefPtr<CefBrowser> browser = CefBrowserHost::GetBrowserByIdentifier(browser_id_);
    if (!browser) {
        return;
    }
    // CanGoBack puede conservar el estado anterior mientras se cargan recursos de la nueva página.
    // GoBack consulta el controlador real de CEF y no retrocede si no existe una entrada anterior.
    // Los cambios de URL seguirán llegando por OnAddressChange.
    browser->GoBack();
    std::fprintf(stderr, "CEF back navigation requested; CEF UI thread: %d.\n", CefCurrentlyOn(TID_UI));
}

void BrowserClient::ResizeBrowser() {
    CEF_REQUIRE_UI_THREAD();
    // Un aviso puede llegar antes de OnAfterCreated o mientras se está cerrando la ventana.
    if (browser_id_ == -1 || close_requested_) {
        return;
    }
    const CefRefPtr<CefBrowser> browser = CefBrowserHost::GetBrowserByIdentifier(browser_id_);
    if (!browser) {
        return;
    }
    const HWND child_window = browser->GetHost()->GetWindowHandle();
    RECT bounds{};
    // Leemos el área Win32 del padre, sin consultar widgets Qt desde este hilo.
    // GetClientRect entrega el área nativa; no usamos las unidades lógicas de QResizeEvent.
    if (child_window == nullptr || !GetClientRect(GetParent(child_window), &bounds)) {
        std::fputs("CEF resize failed: native host area unavailable.\n", stderr);
        return;
    }
    // Ignoramos áreas vacías, por ejemplo durante una transición de minimización.
    if (bounds.right <= 0 || bounds.bottom <= 0) {
        return;
    }
    // Conservamos la posición (0, 0), el orden de ventanas y el foco mientras ajustamos el tamaño.
    if (!SetWindowPos(child_window, nullptr, 0, 0, bounds.right, bounds.bottom,
                      SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        std::fputs("CEF resize failed: native child could not be resized.\n", stderr);
    }
}

void BrowserClient::FocusBrowser() {
    // Comprobamos que la tarea llegó al hilo UI de CEF; aquí no accedemos a widgets Qt.
    CEF_REQUIRE_UI_THREAD();
    // Si ya se solicitó cerrar, no debemos volver a dar el foco al contenido que está saliendo.
    if (close_requested_) {
        return;
    }
    // Guardamos la intención antes de comprobar el identificador. Si aún no existe navegador,
    // OnAfterCreated volverá a llamar a este método para atender la petición pendiente.
    focus_requested_ = true;
    if (browser_id_ == -1) {
        return;
    }
    // El identificador no conserva vivo al navegador. Pedimos a CEF una referencia temporal
    // y comprobamos que todavía existe antes de utilizar su host.
    const CefRefPtr<CefBrowser> browser = CefBrowserHost::GetBrowserByIdentifier(browser_id_);
    if (!browser) {
        return;
    }
    // Consumimos esta petición: si la ventana dejó de estar activa, no la reintentaremos
    // por nuestra cuenta. Un nuevo aviso de Qt podrá solicitar el foco de nuevo.
    focus_requested_ = false;
    const HWND child_window = browser->GetHost()->GetWindowHandle();
    // Verificamos que existe la ventana nativa y que su raíz (la ventana principal Qt)
    // sigue en primer plano. Entre encolar y ejecutar la tarea, el usuario pudo cambiar de app.
    // En ese caso salimos para que esta petición no quite el foco a la otra aplicación.
    if (child_window == nullptr || GetAncestor(child_window, GA_ROOT) != GetForegroundWindow()) {
        return;
    }
    // Solo después de validar todo llamamos a la API de CefBrowserHost.
    // CEF entrega el foco a su contenido; al salir del método se libera la referencia temporal.
    browser->GetHost()->SetFocus(true);
    std::fprintf(stderr, "CEF browser focus forwarded; CEF UI thread: %d.\n",
                 CefCurrentlyOn(TID_UI));
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

void BrowserClient::OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   const CefString& url) {
    CEF_REQUIRE_UI_THREAD();
    // Ignoramos avisos de cierre, de otra instancia o de documentos dentro de un iframe.
    if (close_requested_ || !browser || !frame || !frame->IsMain()
        || browser->GetIdentifier() != browser_id_) {
        return;
    }
    // Verificamos el aviso sin imprimir la URL ni acceder a controles Qt.
    std::fprintf(stderr, "CEF main address changed; length %zu; CEF UI thread: %d.\n",
                 url.length(), CefCurrentlyOn(TID_UI));
    if (callbacks_.on_address_changed) {
        // ToString crea una cadena UTF-8 propia que seguirá viva fuera de este callback CEF.
        callbacks_.on_address_changed(url.ToString());
    }
    // Incluye cambios dentro del mismo documento, como enlaces a fragmentos o history.pushState.
    RefreshBackAvailability(browser);
}

void BrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool, bool, bool) {
    CEF_REQUIRE_UI_THREAD();
    if (close_requested_ || !browser || browser->GetIdentifier() != browser_id_) {
        return;
    }
    RefreshBackAvailability(browser);
}

void BrowserClient::RefreshBackAvailability(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (callbacks_.on_back_availability_changed) {
        // El estado de CanGoBack puede actualizarse al terminar la carga de todos los recursos.
        // La instantánea obtiene el índice actual del historial en este mismo hilo UI.
        browser->GetHost()->GetNavigationEntries(
            new BrowserHistoryVisitor(callbacks_.on_back_availability_changed), true);
    }
}

void BrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    const int browser_id = browser->GetIdentifier();
    // Registramos la instancia para poder localizarla cuando Qt solicite su cierre.
    browser_id_ = browser_id;
    creation_pending_ = false;
    // Recupera el tamaño más reciente si Qt cambió el anfitrión durante la creación asíncrona.
    ResizeBrowser();
    if (focus_requested_) {
        FocusBrowser();
    }
    std::fprintf(stderr, "CEF browser created: id %d.\n", browser_id);
    // Solo entregamos el identificador; no conservamos el navegador ni lo pasamos a la cola Qt.
    if (callbacks_.on_browser_created) {
        callbacks_.on_browser_created(browser_id);
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
    focus_requested_ = false;
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
        }, callbacks_.on_browser_closed))) {
        std::fputs("CEF final close notification could not be posted.\n", stderr);
    }
}

}  // namespace sbp
