#pragma once

#include "include/cef_client.h"
#include "include/cef_display_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"
#include <cstdint>
#include <functional>
#include <string>

namespace sbp {

// Agrupa los avisos del cliente. Se fijan al construirlo y se ejecutan en el hilo UI de CEF.
struct BrowserClientCallbacks {
    std::function<void(int)> on_browser_created;
    std::function<void()> on_browser_closed;
    // Se ejecutará en el hilo UI de CEF cuando retiremos una petición de cierre cancelada.
    std::function<void()> on_browser_close_cancelled;
    // Qt recibe una petición y utiliza la función de respuesta para devolver la decisión del usuario.
    std::function<void(std::function<void(bool)>)> on_page_leave_requested;
    // CEF ha retirado la petición; Qt debe descartar su diálogo si todavía está abierto.
    std::function<void()> on_page_leave_reset;
    // La dirección se entrega como una copia de texto UTF-8, sin compartir objetos de CEF.
    std::function<void(std::string)> on_address_changed;
    // Solo transmitimos la disponibilidad del historial, sin transferir objetos CEF a Qt.
    std::function<void(bool)> on_back_availability_changed;
    // Adelante tiene su aviso propio; utiliza la misma consulta de la entrada actual.
    std::function<void(bool)> on_forward_availability_changed;
    // Recargar se habilita cuando la consulta encuentra una entrada en el historial.
    std::function<void(bool)> on_reload_availability_changed;
};

// Crea una única instancia del navegador y recibe sus avisos de vida, dirección e historial.
// Estos callbacks se ejecutan en el hilo UI de CEF, distinto del hilo principal de Qt.
class BrowserClient final : public CefClient,
                            public CefLifeSpanHandler,
                            public CefDisplayHandler,
                            public CefLoadHandler,
                            public CefJSDialogHandler {
public:
    // Conservamos los avisos juntos, sin añadir un argumento por cada nuevo evento.
    explicit BrowserClient(BrowserClientCallbacks callbacks);

    // Compartimos referencias al cliente, sin copiar el objeto ni su contador.
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

    // Este mismo objeto atiende los avisos del ciclo de vida del navegador.
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;

    // CEF solicita el receptor de los avisos de presentación; este mismo cliente los atiende.
    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override;

    // CEF solicita el receptor de los cambios de carga e historial.
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;

    // CEF obtiene este mismo cliente como receptor de los avisos de diálogos JavaScript.
    CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override;

    // Recibe una copia de los parámetros preparados por Qt y solicita la creación asíncrona.
    // Este método y el estado del cliente pertenecen al hilo UI de CEF.
    void CreateBrowser(CefWindowInfo window_info);

    // Carga la URL validada por la sesión en el documento principal, desde el hilo UI de CEF.
    void Navigate(std::string url);

    // Solicita volver a la entrada anterior del historial, desde el hilo UI de CEF.
    void GoBack();

    // Solicita avanzar a la siguiente entrada del historial, desde el hilo UI de CEF.
    void GoForward();

    // Solicita recargar la página actual, desde el hilo UI de CEF.
    void ReloadPage();

    // Ajusta la ventana hija al área nativa actual de su padre, desde el hilo UI de CEF.
    void ResizeBrowser();

    // Recibe la petición Qt y entrega el foco al navegador desde el hilo UI de CEF.
    void FocusBrowser();

    // Solicita el cierre y espera OnBeforeClose, incluso si la creación todavía está pendiente.
    void RequestClose();

    // Retira la petición pendiente en el hilo UI de CEF y comunica su cancelación mediante el callback.
    void CancelClose();

    // CEF solicita confirmar el abandono; enviamos la pregunta a Qt sin bloquear el hilo UI de CEF.
    bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser> browser, const CefString& message_text,
                              bool is_reload, CefRefPtr<CefJSDialogCallback> callback) override;

    // CEF retira los diálogos pendientes al cambiar de documento o cancelar su petición.
    void OnResetDialogState(CefRefPtr<CefBrowser> browser) override;

    // CEF entrega la dirección real del frame que ha cambiado; filtramos el documento principal.
    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                         const CefString& url) override;

    // CEF avisa de cambios de carga; aprovechamos el aviso para consultar la entrada actual.
    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool is_loading,
                              bool can_go_back, bool can_go_forward) override;

    // Marca cuándo se confirma el documento principal; todavía puede quedar contenido por cargar o pintar.
    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     TransitionType transition_type) override;

    // Registra fallos del documento principal, sin incluir la URL ni el texto recibido de CEF.
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode error_code, const CefString& error_text,
                     const CefString& failed_url) override;

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

    // Consume una única respuesta en CEF; el identificador descarta respuestas de peticiones anteriores.
    void CompletePageLeave(std::uint64_t request_id, bool allow_leave);
    // Retira la pregunta de Qt y resuelve cualquier callback CEF pendiente como cancelado.
    void ResetPageLeave();

    // Consulta una sola entrada sin esperar al final de la carga; los avisos actualizan ambas flechas en Qt.
    void RefreshNavigationAvailability(CefRefPtr<CefBrowser> browser);

    // Se fijan al construir el cliente; quien las proporciona controla la vida de sus capturas.
    const BrowserClientCallbacks callbacks_;

    // Solo se lee o modifica en el hilo UI de CEF. -1 significa que no hay navegador registrado.
    // Guardamos un identificador, sin mantener una referencia circular navegador -> cliente -> navegador.
    int browser_id_ = -1;
    bool creation_pending_ = false;
    bool close_requested_ = false;
    bool close_notified_ = false;
    // Conserva una petición anterior a OnAfterCreated; solo se utiliza en el hilo UI de CEF.
    bool focus_requested_ = false;

    // El callback nativo permanece en CEF; Qt recibe únicamente una función de respuesta con un booleano.
    CefRefPtr<CefJSDialogCallback> page_leave_callback_;
    // Cada pregunta tiene su propio identificador, aunque la respuesta Qt llegue después de cancelarla.
    std::uint64_t page_leave_request_id_ = 0;

    // CEF implementa el contador que utilizan las referencias administradas al cliente.
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace sbp
