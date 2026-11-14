#pragma once

#include "include/cef_app.h"
#include "include/cef_browser_process_handler.h"

#include <functional>

namespace sbp {

// Atiende los avisos de aplicación y del contexto CEF en el proceso principal.
class BrowserApp final : public CefApp, public CefBrowserProcessHandler {
public:
    // Conservamos la función que ejecutaremos al recibir el aviso de contexto preparado.
    // std::move transfiere la función al miembro; BrowserApp no necesita depender de Qt.
    explicit BrowserApp(std::function<void()> on_context_initialized);

    // Se comparten referencias al mismo objeto; no se copia el objeto ni su contador.
    BrowserApp(const BrowserApp&) = delete;
    BrowserApp& operator=(const BrowserApp&) = delete;

    // Aplica las opciones de seguridad antes de inicializar Chromium en el proceso principal.
    void OnBeforeCommandLineProcessing(const CefString& processType,
                                       CefRefPtr<CefCommandLine> commandLine) override;

    // CEF solicita el receptor de avisos del proceso principal; este mismo objeto lo implementa.
    // La devolución crea una referencia administrada al objeto existente, sin construir otro.
    CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override;

    // CEF llama a este método cuando su contexto está preparado, en su hilo de interfaz.
    void OnContextInitialized() override;

private:
    // La última referencia libera el objeto; no debe destruirse manualmente desde fuera.
    ~BrowserApp() override;

    // Se fija al construir el objeto y no se modifica mientras CEF lo utiliza.
    const std::function<void()> on_context_initialized_;

    // Macro de CEF que implementa AddRef, Release y el contador de referencias.
    IMPLEMENT_REFCOUNTING(BrowserApp);
};

}  // namespace sbp
