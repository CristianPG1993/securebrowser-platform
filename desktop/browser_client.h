#pragma once

#include "include/cef_client.h"
#include "include/cef_life_span_handler.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>

namespace sbp {

// Recibirá los avisos de una instancia del navegador cuando la creemos.
// Sus callbacks de vida se ejecutan en el hilo UI de CEF, distinto del hilo principal de Qt.
class BrowserClient final : public CefClient, public CefLifeSpanHandler {
public:
    BrowserClient() = default;

    // Compartimos referencias al cliente, sin copiar el objeto ni su contador.
    BrowserClient(const BrowserClient&) = delete;
    BrowserClient& operator=(const BrowserClient&) = delete;

    // Este mismo objeto atiende los avisos del ciclo de vida del navegador.
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
        return this;
    }

    // CEF notificará aquí que una instancia del navegador ya ha sido creada.
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override {
        CEF_REQUIRE_UI_THREAD();
        // La referencia recibida vive durante el callback; no la guardamos en un miembro.
        std::fprintf(stderr, "CEF browser created: id %d.\n", browser->GetIdentifier());
    }

    // Este aviso forma parte del cierre; no sustituye la solicitud de cerrar el navegador.
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override {
        CEF_REQUIRE_UI_THREAD();
        // Solo observamos su identificador, sin conservar el navegador ni acceder a widgets Qt.
        std::fprintf(stderr, "CEF browser closing: id %d.\n", browser->GetIdentifier());
    }

private:
    // La última referencia libera el cliente; su vida se gestiona mediante CefRefPtr.
    ~BrowserClient() override = default;

    // CEF implementa el contador que utilizan las referencias administradas al cliente.
    IMPLEMENT_REFCOUNTING(BrowserClient);
};

}  // namespace sbp
