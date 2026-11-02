#pragma once

#include "ui/browser_host.h"

#include <QLineEdit>
#include <QObject>
#include <QVBoxLayout>

#include <functional>

class QEvent;

namespace sbp {

class BrowserWindow;

// Reúne la construcción y distribución de los controles de la ventana, sin depender de CEF.
// Se crea después de BrowserWindow y se destruye antes de ella, siempre en el hilo Qt.
class BrowserWindowUi final : public QObject {
public:
    // Los avisos del anfitrión se reciben ya preparados por quien coordina la sesión.
    BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks);
    BrowserWindowUi(const BrowserWindowUi&) = delete;
    BrowserWindowUi& operator=(const BrowserWindowUi&) = delete;

    // La sesión necesita el anfitrión para obtener su ventana nativa y coordinar el foco.
    BrowserHost& GetBrowserHost();

    // Recibe la URL actual en el hilo Qt; conserva la edición si la barra tiene el foco.
    void SetAddress(const QString& address);

    // Retira el foco lógico y el caret de la barra cuando el teclado ya pertenece a CEF.
    void ClearAddressFocus();

    // Guarda la función que recibirá el texto al pulsar Enter; instalarla no la ejecuta.
    void SetAddressSubmitHandler(std::function<void(const QString&)> on_address_submitted);

protected:
    // Qt entrega aquí los eventos del campo donde instalamos este objeto como filtro.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // El orden permite destruir los controles antes del layout y de la ventana que los contiene.
    // Layout vertical que distribuye el campo de dirección arriba y el contenido web debajo.
    QVBoxLayout window_layout_;

    // Campo de texto Qt donde el usuario escribe y edita la dirección web.
    QLineEdit address_input_;

    // Última URL recibida del navegador; permite mostrarla al terminar de editar la barra.
    QString latest_address_;

    // Widget Qt con ventana nativa que aloja la ventana hija del navegador CEF.
    BrowserHost browser_host_;

    // Aviso síncrono de la interfaz a la sesión; ambos trabajan en el hilo principal Qt.
    std::function<void(const QString&)> on_address_submitted_;
};

}  // namespace sbp
