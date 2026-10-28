#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include <functional>

namespace sbp {

// Recibirá los avisos de una instancia del navegador cuando la creemos.
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

    // Se llama en el hilo UI de CEF. Esta etapa solo confirma el caso sin navegador creado.
    void RequestClose();

    // CEF notificará aquí que una instancia del navegador ya ha sido creada.
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;

    // Este aviso forma parte del cierre; no sustituye la solicitud de cerrar el navegador.
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

private:
    // La última referencia libera el cliente; su vida se gestiona mediante CefRefPtr.
    ~BrowserClient() override;

    // Se fijan al construir el cliente; quien las proporciona controla la vida de sus capturas.
    const std::function<void(int)> on_browser_created_;
    const std::function<void()> on_browser_closed_;

    // CEF implementa el contador que utilizan las referencias administradas al cliente.
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace sbp
