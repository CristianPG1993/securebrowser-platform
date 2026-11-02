#include "ui/browser_window_ui.h"

#include "ui/browser_window.h"

#include <QEvent>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QStyle>

#include <cstdio>

#include <windows.h>

#include <utility>

namespace sbp {

BrowserWindowUi::BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks)
    : QObject(&window),
      window_layout_(&window),
      back_button_(&window),
      address_input_(&window),
      browser_host_(std::move(host_callbacks), &window) {
    // Aquí se configura el aspecto inicial de la ventana y la distribución de sus controles.
    window.setWindowTitle("SecureBrowser");
    window.resize(1024, 768);
    window_layout_.setContentsMargins(0, 0, 0, 0);

    // CEF habilitará el botón cuando exista una página anterior en su historial.
    back_button_.setObjectName("back_button");
    // El icono estándar de Qt adapta la flecha al estilo y al estado habilitado del botón.
    back_button_.setIcon(window.style()->standardIcon(QStyle::SP_ArrowBack));
    back_button_.setToolTip("Atrás");
    back_button_.setAccessibleName("Volver a la página anterior");
    back_button_.setEnabled(false);
    back_button_.installEventFilter(this);
    ConnectBackButton();

    // El campo permite editar una dirección; Enter avisará a la sesión mediante su callback.
    address_input_.setObjectName("address_input");
    address_input_.setAccessibleName("Dirección web");
    address_input_.setPlaceholderText("Introduce una URL HTTP o HTTPS");
    // Qt consultará eventFilter antes de entregar los eventos de este campo.
    // No requiere Q_OBJECT ni importar datos de QLineEdit para conectar una señal tipada.
    address_input_.installEventFilter(this);

    // La fila coloca el botón a la izquierda; el factor 1 permite que la dirección ocupe el espacio restante.
    navigation_layout_.addWidget(&back_button_);
    navigation_layout_.addWidget(&address_input_, 1);
    // El layout principal mantiene la barra completa encima del contenido web.
    window_layout_.addLayout(&navigation_layout_);
    window_layout_.addWidget(&browser_host_);
}

BrowserHost& BrowserWindowUi::GetBrowserHost() {
    // Devuelve el control existente por referencia; no construye ni copia otro anfitrión.
    return browser_host_;
}

void BrowserWindowUi::SetAddress(const QString& address) {
    // Recordamos siempre la URL real, aunque el usuario esté escribiendo otra dirección.
    latest_address_ = address;
    // Tener el foco reserva el texto y la selección para la edición del usuario.
    if (address_input_.hasFocus()) {
        return;
    }
    // QLineEdit guarda el texto y Qt programa su repintado para mostrar la nueva dirección.
    address_input_.setText(address);
}

void BrowserWindowUi::ClearAddressFocus() {
    // clearFocus entrega a QLineEdit su FocusOut normal, que detiene el caret.
    if (address_input_.hasFocus()) {
        address_input_.clearFocus();
    }
}

void BrowserWindowUi::SetAddressSubmitHandler(
    std::function<void(const QString&)> on_address_submitted) {
    on_address_submitted_ = std::move(on_address_submitted);
}

void BrowserWindowUi::SetBackRequestHandler(std::function<void()> on_back_requested) {
    // Guardamos la acción; clicked la ejecutará a través de RequestBack cuando se active el botón.
    on_back_requested_ = std::move(on_back_requested);
}

void BrowserWindowUi::SetBackAvailable(bool available) {
    back_button_.setEnabled(available);
}

void BrowserWindowUi::ConnectBackButton() {
    // Consultamos la señal en tiempo de ejecución para conservar la carga diferida de Qt en el bootstrap.
    // QMetaObject::connect entrega la señal a una lambda, sin añadir Q_OBJECT a nuestra clase.
    const QMetaObject* button_meta = back_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // La lambda llama al método privado sin importar el staticMetaObject de nuestra clase base.
    const auto connection = QMetaObject::connect(&back_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestBack(); });
    if (!connection) {
        std::fputs("Qt back button activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::RequestBack() {
    // Qt ya ha reconocido la activación con ratón o teclado; ejecutamos la acción guardada.
    if (on_back_requested_) {
        on_back_requested_();
    }
}

bool BrowserWindowUi::eventFilter(QObject* watched, QEvent* event) {
    // Al abandonar la barra, mostramos la última URL recibida, incluso si hubo avisos durante la edición.
    if (watched == &address_input_ && event->type() == QEvent::FocusOut) {
        address_input_.setText(latest_address_);
    }
    // CEF puede conservar el foco nativo aunque Qt recuerde un control de la barra como enfocado.
    // Al entrar con ratón o teclado, devolvemos los mensajes de Windows a la ventana Qt.
    if ((watched == &address_input_ || watched == &back_button_)
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)) {
        QWidget* qt_window = address_input_.window();
        if (qt_window->isActiveWindow()) {
            const HWND qt_window_handle = reinterpret_cast<HWND>(qt_window->winId());
            // Solo cambiamos el foco real si sigue en otra ventana; Qt conserva su control de destino.
            if (GetFocus() != qt_window_handle) {
                ::SetFocus(qt_window_handle);
            }
        }
    }
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
