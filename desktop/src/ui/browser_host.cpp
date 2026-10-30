#include "browser_host.h"

#include <QFocusEvent>
#include <QResizeEvent>

#include <utility>

namespace sbp {

BrowserHost::BrowserHost(BrowserHostCallbacks callbacks, QWidget* parent)
    : QWidget(parent), callbacks_(std::move(callbacks)) {
    // El anfitrión necesita un HWND propio para contener la ventana nativa de CEF.
    setAttribute(Qt::WA_NativeWindow);
    // Permite que Qt entregue el foco al anfitrión mediante teclado o ratón.
    setFocusPolicy(Qt::StrongFocus);
}

void BrowserHost::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Solo avisamos: la función registrada publicará el trabajo en el hilo UI de CEF.
    if (callbacks_.on_resized) {
        callbacks_.on_resized();
    }
}

bool BrowserHost::event(QEvent* event) {
    const bool handled = QWidget::event(event);
    // Qt también propaga la activación de la ventana a los widgets hijos visibles.
    // Solo restauramos CEF si el anfitrión sigue siendo el destino del teclado en Qt.
    if (event->type() == QEvent::WindowActivate && hasFocus() && callbacks_.on_focus_requested) {
        callbacks_.on_focus_requested();
    }
    return handled;
}

void BrowserHost::focusInEvent(QFocusEvent* event) {
    QWidget::focusInEvent(event);
    if (callbacks_.on_focus_requested) {
        callbacks_.on_focus_requested();
    }
    // No enviamos una pérdida de foco al salir hacia la hija: desharía esta entrega a CEF.
}

}  // namespace sbp
