#include "app/browser_lifecycle.h"

#include "ui/browser_window.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"

#include <QMetaObject>
#include <QThread>

#include <cstdio>
#include <utility>

namespace sbp {

void BrowserLifecycle::OnContextInitialized() {
    std::fprintf(stderr, "CEF context notification received; Qt main thread: %d.\n",
                 QThread::isMainThread());
    cef_context_ready_ = true;
    PrepareBrowserWindowInfo();
}

void BrowserLifecycle::OnHostReady(HWND host_handle) {
    host_handle_ = host_handle;
    std::fprintf(stderr, "Qt native browser host ready: %p.\n", static_cast<void*>(host_handle_));
    PrepareBrowserWindowInfo();
}

void BrowserLifecycle::PrepareBrowserWindowInfo() {
    // Los avisos de contexto y de anfitrión pueden llegar en cualquier orden.
    if (!cef_context_ready_ || host_handle_ == nullptr || window_info_ready_ || close_requested_) {
        return;
    }
    RECT host_bounds{};
    if (!GetClientRect(host_handle_, &host_bounds)
        || host_bounds.right <= 0 || host_bounds.bottom <= 0) {
        std::fputs("CEF window preparation failed: native host area unavailable.\n", stderr);
        return;
    }
    // Se preparan los parámetros de la ventana hija; la creación se solicita después.
    window_info_.SetAsChild(host_handle_,
        CefRect(0, 0, static_cast<int>(host_bounds.right), static_cast<int>(host_bounds.bottom)));
    window_info_.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window_info_.windowless_rendering_enabled = false;
    window_info_ready_ = true;
    std::fprintf(stderr,
                 "CEF child window parameters prepared: parent %p; size %dx%d; runtime style %d; Qt main thread: %d.\n",
                 static_cast<void*>(window_info_.parent_window), window_info_.bounds.width,
                 window_info_.bounds.height, static_cast<int>(window_info_.runtime_style),
                 QThread::isMainThread());
    request_browser_creation_(window_info_);
}

BrowserClientCallbacks BrowserLifecycle::MakeClientCallbacks(
    BrowserWindow& window, BrowserClientCallbacks callbacks) {
    // CEF entrega solo el identificador; Qt recibe una copia mediante su cola de eventos.
    callbacks.on_browser_created = [&window](int browser_id) {
        // Esta lambda interior pertenece al hilo Qt y no conserva objetos del navegador.
        const bool queued = QMetaObject::invokeMethod(&window, [browser_id] {
            std::fprintf(stderr, "Qt received browser creation: id %d; Qt main thread: %d.\n",
                         browser_id, QThread::isMainThread());
        }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF browser creation notification could not be queued to Qt.\n", stderr);
        }
    };
    // El rechazo llega desde CEF; publicamos el fallo antes de su confirmación final de cierre.
    callbacks.on_browser_creation_failed = [this, &window] {
        // Solo el hilo Qt modifica este estado; la sesión lo conserva hasta después del apagado CEF.
        const bool queued = QMetaObject::invokeMethod(&window, [this] {
            exit_code_ = 1;
            close_requested_ = true;
            std::fputs("Qt received CEF browser creation failure; exit code 1.\n", stderr);
        }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF browser creation failure could not be queued to Qt.\n", stderr);
        }
    };
    // El cierre CEF publica su confirmación; Qt mantiene la ventana viva hasta recibirla.
    callbacks.on_browser_closed = [this, &window] {
        // Solo Qt consulta el anfitrión y confirma el cierre de su ventana.
        const bool queued = QMetaObject::invokeMethod(&window, [this, &window] {
            std::fprintf(stderr, "Qt close confirmed; native host valid: %d.\n",
                         IsWindow(host_handle_) != FALSE);
            window.ConfirmClose();
        }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("Qt close confirmation could not be queued.\n", stderr);
        }
    };
    // El cliente emite la cancelación en CEF; publicamos su recepción para el hilo principal Qt.
    callbacks.on_browser_close_cancelled = [this, &window] {
        // Solo esta lambda interior cambia el estado de la coordinación y de la ventana Qt.
        const bool queued = QMetaObject::invokeMethod(&window, [this, &window] {
            if (!close_requested_) {
                return;
            }
            close_requested_ = false;
            std::fprintf(stderr, "Qt received CEF close cancellation; Qt main thread: %d.\n",
                         QThread::isMainThread());
            window.CancelClose();
        }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("CEF close cancellation could not be queued to Qt.\n", stderr);
        }
    };
    return callbacks;
}

void BrowserLifecycle::Connect(BrowserWindow& window, CefRefPtr<BrowserClient> browser_client) {
    // Qt solicita la creación cuando contexto y anfitrión están listos; la tarea conserva el cliente.
    request_browser_creation_ = [this, browser_client, &window](const CefWindowInfo& window_info) {
        if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::CreateBrowser,
                                               browser_client, window_info))) {
            std::fputs("CEF browser creation task could not be posted.\n", stderr);
            exit_code_ = 1;
            close_requested_ = true;
            window.ConfirmClose();
        }
    };
    // El primer cierre Qt impide nuevas creaciones y publica la petición de cierre para CEF.
    window.SetCloseRequestHandler([this, browser_client] {
        close_requested_ = true;
        if (!CefPostTask(TID_UI, base::BindOnce(&BrowserClient::RequestClose, browser_client))) {
            std::fputs("Qt close request could not be posted to CEF.\n", stderr);
        }
    });
}

void BrowserLifecycle::Detach() {
    request_browser_creation_ = {};
    host_handle_ = nullptr;
    window_info_ = CefWindowInfo();
    window_info_ready_ = false;
}

int BrowserLifecycle::ExitCode(int event_loop_exit_code) const {
    // Un fallo previo al bucle conserva su código aunque Qt termine normalmente.
    return exit_code_ != 0 ? exit_code_ : event_loop_exit_code;
}

const bool& BrowserLifecycle::CloseRequested() const {
    return close_requested_;
}

const HWND& BrowserLifecycle::HostHandle() const {
    return host_handle_;
}

}  // namespace sbp
