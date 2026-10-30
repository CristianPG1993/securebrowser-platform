#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>
#include <functional>
#include <utility>

namespace sbp {

// Atiende los avisos de aplicación y del contexto CEF en el proceso principal.
class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
public:
    // Conservamos la función que ejecutaremos al recibir el aviso de contexto preparado.
    // std::move transfiere la función al miembro; BrowserApp no necesita depender de Qt.
    explicit BrowserApp(std::function<void()> on_context_initialized)
        : on_context_initialized_(std::move(on_context_initialized)) {}

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
        // Los widgets Qt pertenecen al hilo principal; aquí no accedemos a ellos.
        std::fprintf(stderr, "CEF context initialized; CEF UI thread: %d.\n",
                     CefCurrentlyOn(TID_UI));
        // Esta función aún se ejecuta en el hilo CEF: quien la proporciona debe encolar el salto a Qt.
        if (on_context_initialized_) {
            on_context_initialized_();
        }
    }

private:
    // La última referencia libera el objeto; no debe destruirse manualmente desde fuera.
    ~BrowserApp() override = default;

    // Se fija al construir el objeto y no se modifica mientras CEF lo utiliza.
    const std::function<void()> on_context_initialized_;

    // Macro de CEF que implementa AddRef, Release y el contador de referencias.
    IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace sbp
