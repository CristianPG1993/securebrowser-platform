#pragma once

#include "include/internal/cef_win.h"

#include <functional>

class QApplication;

namespace sbp {

// Coordina la ventana Qt y su navegador CEF; el arranque de Windows queda fuera.
// Todos sus métodos y su estado pertenecen al hilo principal de Qt.
class BrowserSession final {
public:
    BrowserSession() = default;
    BrowserSession(const BrowserSession&) = delete;
    BrowserSession& operator=(const BrowserSession&) = delete;

    // Recibe en Qt el aviso que BrowserApp ha enviado desde el hilo de CEF.
    void OnContextInitialized();

    // Crea la interfaz, conecta los avisos y ejecuta el bucle Qt una sola vez.
    // Devuelve el código de salida después de destruir los widgets de esta sesión.
    int Run(QApplication& qt_app);

private:
    // Solicita el navegador cuando están disponibles el contexto CEF y el anfitrión Qt.
    void PrepareBrowserWindowInfo();

    // La sesión vive hasta después de CefShutdown, aunque Run ya haya destruido la ventana.
    bool cef_context_ready_ = false;
    HWND browser_host_handle_ = nullptr;
    CefWindowInfo browser_window_info_;
    bool browser_window_info_ready_ = false;
    bool qt_close_requested_ = false;
    // Solo se instala mientras vive la ventana; se retira antes de destruir sus widgets.
    std::function<void(const CefWindowInfo&)> request_browser_creation_;
};

}  // namespace sbp
