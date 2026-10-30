#include "browser_window.h"

#include <QCloseEvent>
#include <QMetaObject>

#include <windows.h>

#include <cstdio>
#include <utility>

namespace sbp {

// Utilizamos la construcción predeterminada de QWidget y el estado inicial declarado en el header.
BrowserWindow::BrowserWindow() = default;

void BrowserWindow::SetCloseRequestHandler(std::function<void()> on_close_requested) {
    // Conservamos la función que se ejecutará cuando Qt entregue la primera solicitud de cierre.
    on_close_requested_ = std::move(on_close_requested);
}

void BrowserWindow::SetNativeFocusHandler(std::function<void()> on_native_focus) {
    on_native_focus_ = std::move(on_native_focus);
}

bool BrowserWindow::nativeEvent(const QByteArray& event_type, void* message, qintptr* result) {
    if (event_type == "windows_generic_MSG" && static_cast<MSG*>(message)->message == WM_SETFOCUS) {
        // Esperamos a que Qt termine de procesar el foco nativo antes de solicitar la entrega a CEF.
        // Qt descarta esta lambda si se destruye la ventana receptora antes de ejecutarla.
        const bool queued = QMetaObject::invokeMethod(this, [this] {
            if (!close_requested_ && on_native_focus_) {
                on_native_focus_();
            }
        }, Qt::QueuedConnection);
        if (!queued) {
            std::fputs("Qt native focus notification could not be queued.\n", stderr);
        }
    }
    return QWidget::nativeEvent(event_type, message, result);
}

void BrowserWindow::ConfirmClose() {
    if (close_confirmed_) {
        return;
    }
    close_confirmed_ = true;
    // close() provoca otro closeEvent, que ahora encontrará el cierre confirmado.
    close();
}

void BrowserWindow::closeEvent(QCloseEvent* event) {
    if (close_confirmed_) {
        std::fputs("Qt close accepted after confirmation.\n", stderr);
        // La implementación de QWidget acepta el evento y permite ocultar la ventana.
        QWidget::closeEvent(event);
        return;
    }

    // Rechazamos el cierre inmediato para conservar la ventana y su anfitrión nativo.
    event->ignore();
    if (close_requested_) {
        return;
    }
    // Una segunda pulsación de la X no inicia otra solicitud mientras esperamos.
    close_requested_ = true;
    std::fputs("Qt close requested; native host remains alive.\n", stderr);
    if (on_close_requested_) {
        on_close_requested_();
    }
}

}  // namespace sbp
