#pragma once

#include "browser/browser_client.h"

namespace sbp {

class BrowserWindow;

// Coordina exclusivamente la creación y el cierre del navegador y de su anfitrión.
// Vive en la sesión hasta después de CefShutdown; su estado se usa solo desde Qt.
class BrowserLifecycle final {
public:
    BrowserLifecycle() = default;
    BrowserLifecycle(const BrowserLifecycle&) = delete;
    BrowserLifecycle& operator=(const BrowserLifecycle&) = delete;

    void OnContextInitialized();
    void OnHostReady(HWND host_handle);

    // Añade los avisos de vida al bloque de callbacks que prepara el módulo de navegación.
    BrowserClientCallbacks MakeClientCallbacks(
        BrowserWindow& window, BrowserClientCallbacks callbacks);
    void Connect(BrowserWindow& window, CefRefPtr<BrowserClient> browser_client);

    // Retira referencias al cliente y al anfitrión antes de destruir la ventana.
    void Detach();
    int ExitCode(int event_loop_exit_code) const;

    // Referencias estables para los módulos Qt; siguen vivas durante sus callbacks.
    const bool& CloseRequested() const;
    const HWND& HostHandle() const;

private:
    void PrepareBrowserWindowInfo();

    bool cef_context_ready_ = false;
    HWND host_handle_ = nullptr;
    CefWindowInfo window_info_;
    bool window_info_ready_ = false;
    bool close_requested_ = false;
    int exit_code_ = 0;
    std::function<void(const CefWindowInfo&)> request_browser_creation_;
};

}  // namespace sbp
