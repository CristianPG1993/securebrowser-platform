#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>

namespace sbp {

// Atiende los avisos de aplicación y del contexto CEF en el proceso principal.
class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
public:
    BrowserApp() = default;

    // Se comparten referencias al mismo objeto; no se copia el objeto ni su contador.
    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    // CEF solicita el receptor de avisos del proceso principal; este mismo objeto lo implementa.
    // La devolución crea una referencia administrada al objeto existente, sin construir otro.
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
        return this;
    }

    // CEF llama a este método cuando su contexto está preparado, en su hilo de interfaz.
    void OnContextInitialized() override {
        // En Debug, esta macro comprueba que estamos en TID_UI, el hilo de interfaz de CEF.
        CEF_REQUIRE_UI_THREAD();
        // Registramos también el resultado en Release: 1 confirma el hilo esperado.
        // Aquí solo observamos el aviso; los widgets Qt pertenecen al hilo principal.
        std::fprintf(stderr, "CEF context initialized; CEF UI thread: %d.\n",
                     CefCurrentlyOn(TID_UI));
    }

private:
    // La última referencia libera el objeto; no debe destruirse manualmente desde fuera.
    ~BrowserApp() override = default;

    // Macro de CEF que implementa AddRef, Release y el contador de referencias.
    IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace sbp
