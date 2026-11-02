#include "browser/browser_app.h"

#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <cstdio>
#include <utility>

namespace sbp {

BrowserApp::BrowserApp(std::function<void()> on_context_initialized)
    : on_context_initialized_(std::move(on_context_initialized)) {}

BrowserApp::~BrowserApp() = default;

CefRefPtr<CefBrowserProcessHandler> BrowserApp::GetBrowserProcessHandler() {
    return this;
}

void BrowserApp::OnContextInitialized() {
    // En Debug, esta macro comprueba que estamos en TID_UI, el hilo de interfaz de CEF.
    CEF_REQUIRE_UI_THREAD();
    // Registramos también el resultado en Release: 1 confirma el hilo esperado.
    // Los widgets Qt pertenecen al hilo principal; aquí no accedemos a ellos.
    std::fprintf(stderr, "CEF context initialized; CEF UI thread: %d.\n", CefCurrentlyOn(TID_UI));
    // Esta función aún se ejecuta en el hilo CEF: quien la proporciona debe encolar el salto a Qt.
    if (on_context_initialized_) {
        on_context_initialized_();
    }
}

}  // namespace sbp
