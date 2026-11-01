#include "ui/browser_window_ui.h"

#include "ui/browser_window.h"

#include <QEvent>
#include <QKeyEvent>

#include <utility>

namespace sbp {

BrowserWindowUi::BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks)
    : QObject(&window),
      window_layout_(&window),
      address_input_(&window),
      browser_host_(std::move(host_callbacks), &window) {
    // Aquí se configura el aspecto inicial de la ventana y la distribución de sus controles.
    window.setWindowTitle("SecureBrowser");
    window.resize(1024, 768);
    window_layout_.setContentsMargins(0, 0, 0, 0);

    // El campo permite editar una dirección; Enter avisará a la sesión mediante su callback.
    address_input_.setObjectName("address_input");
    address_input_.setAccessibleName("Dirección web");
    address_input_.setPlaceholderText("Introduce una URL HTTP o HTTPS");
    // Qt consultará eventFilter antes de entregar los eventos de este campo.
    // No requiere Q_OBJECT ni importar datos de QLineEdit para conectar una señal tipada.
    address_input_.installEventFilter(this);

    // El orden del layout coloca la dirección arriba y reserva el espacio restante al contenido web.
    window_layout_.addWidget(&address_input_);
    window_layout_.addWidget(&browser_host_);
}

BrowserHost& BrowserWindowUi::GetBrowserHost() {
    // Devuelve el control existente por referencia; no construye ni copia otro anfitrión.
    return browser_host_;
}

void BrowserWindowUi::SetAddressSubmitHandler(
    std::function<void(const QString&)> on_address_submitted) {
    on_address_submitted_ = std::move(on_address_submitted);
}

bool BrowserWindowUi::eventFilter(QObject* watched, QEvent* event) {
    // Solo observamos pulsaciones del campo de dirección; el resto mantiene su comportamiento Qt.
    if (watched == &address_input_ && event->type() == QEvent::KeyPress) {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        // Qt distingue Enter del teclado principal y Enter del teclado numérico.
        if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
            // Mantener Enter pulsado no debe producir solicitudes repetidas automáticamente.
            if (!key_event->isAutoRepeat() && on_address_submitted_) {
                // Conservamos una copia del texto durante la llamada; no se transforma ni valida aún.
                const QString address = address_input_.text();
                on_address_submitted_(address);
            }
            // La pulsación ya está atendida; no la propagamos a otros controles de la ventana.
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace sbp
