#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include <functional>

namespace sbp {

// Crea una única instancia del navegador y recibe los avisos de su ciclo de vida.
// Sus callbacks de vida se ejecutan en el hilo UI de CEF, distinto del hilo principal de Qt.
class BrowserClient final : public CefClient, public CefLifeSpanHandler {
public:
    // Conservamos las funciones que recibirán el identificador creado y el aviso de cierre.
    explicit BrowserClient(std::function<void(int)> on_browser_created,
                           std::function<void()> on_browser_closed);

    // Compartimos referencias al cliente, sin copiar el objeto ni su contador.
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

    // Este mismo objeto atiende los avisos del ciclo de vida del navegador.
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;

    // Recibe una copia de los parámetros preparados por Qt y solicita la creación asíncrona.
    // Este método y el estado del cliente pertenecen al hilo UI de CEF.
    void CreateBrowser(CefWindowInfo window_info);

    // Ajusta la ventana hija al área nativa actual de su padre, desde el hilo UI de CEF.
    void ResizeBrowser();

    // Recibe la petición Qt y entrega el foco al navegador desde el hilo UI de CEF.
    void FocusBrowser();

    // Solicita el cierre y espera OnBeforeClose, incluso si la creación todavía está pendiente.
    void RequestClose();

    // CEF notificará aquí que una instancia del navegador ya ha sido creada.
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

    // Cierra la ventana hija CEF sin destruir todavía el widget anfitrión de Qt.
    bool DoClose(CefRefPtr<CefBrowser> browser) override;

    // Esta etapa admite un único navegador; las solicitudes de ventanas emergentes se cancelan.
    bool OnBeforePopup(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                       int popup_id, const CefString& target_url, const CefString& target_frame_name,
                       WindowOpenDisposition target_disposition, bool user_gesture,
                       const CefPopupFeatures& popup_features, CefWindowInfo& window_info,
                       CefRefPtr<CefClient>& client, CefBrowserSettings& settings,
                       CefRefPtr<CefDictionaryValue>& extra_info, bool* no_javascript_access) override;

    // Este aviso forma parte del cierre; no sustituye la solicitud de cerrar el navegador.
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

private:
    // La última referencia libera el cliente; su vida se gestiona mediante CefRefPtr.
    ~BrowserClient() override;

    // Publica una sola confirmación después de que termine el callback CEF actual.
    void NotifyClosed();

    // Se fijan al construir el cliente; quien las proporciona controla la vida de sus capturas.
    const std::function<void(int)> on_browser_created_;
    const std::function<void()> on_browser_closed_;

    // Solo se lee o modifica en el hilo UI de CEF. -1 significa que no hay navegador registrado.
    // Guardamos un identificador, sin mantener una referencia circular navegador -> cliente -> navegador.
    int browser_id_ = -1;
    bool creation_pending_ = false;
    bool close_requested_ = false;
    bool close_notified_ = false;
    // Conserva una petición anterior a OnAfterCreated; solo se utiliza en el hilo UI de CEF.
    bool focus_requested_ = false;

    // CEF implementa el contador que utilizan las referencias administradas al cliente.
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace sbp
