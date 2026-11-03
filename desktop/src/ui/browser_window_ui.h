#pragma once

#include "ui/browser_host.h"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
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

    // Guarda la función que atenderá la petición de volver atrás; instalarla no navega.
    void SetBackRequestHandler(std::function<void()> on_back_requested);

    // CEF comunica su historial mediante una tarea Qt; el botón refleja si se puede retroceder.
    void SetBackAvailable(bool available);

    // Guarda la acción Adelante; la sesión la proporcionará al conectar este control con CEF.
    void SetForwardRequestHandler(std::function<void()> on_forward_requested);

    // Guarda la acción Recargar; la sesión la proporcionará al conectar este control con CEF.
    void SetReloadRequestHandler(std::function<void()> on_reload_requested);

    // Actualiza en Qt si se puede recargar la página actual.
    void SetReloadAvailable(bool available);

    // Actualiza en Qt si existe una entrada posterior en el historial.
    void SetForwardAvailable(bool available);

protected:
    // Qt entrega aquí los eventos del campo donde instalamos este objeto como filtro.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Conecta la activación normal del botón Qt con la acción proporcionada por la sesión.
    void ConnectBackButton();
    void RequestBack();
    void ConnectForwardButton();
    void RequestForward();
    void ConnectReloadButton();
    void RequestReload();

    // El orden permite destruir los controles antes del layout y de la ventana que los contiene.
    // Layout vertical que coloca la barra de navegación arriba y el contenido web debajo.
    QVBoxLayout window_layout_;

    // Fila horizontal que reúne las flechas de navegación y el campo de dirección.
    QHBoxLayout navigation_layout_;

    // Botón de navegación; CEF determina su disponibilidad a partir del historial.
    QPushButton back_button_;

    // Flecha Adelante; empieza deshabilitada hasta recibir la disponibilidad desde CEF.
    QPushButton forward_button_;

    // Botón Recargar; empieza deshabilitado hasta recibir la disponibilidad desde CEF.
    QPushButton reload_button_;

    // Campo de texto Qt donde el usuario escribe y edita la dirección web.
    QLineEdit address_input_;

    // Última URL recibida del navegador; permite mostrarla al terminar de editar la barra.
    QString latest_address_;

    // Widget Qt con ventana nativa que aloja la ventana hija del navegador CEF.
    BrowserHost browser_host_;

    // Aviso síncrono de la interfaz a la sesión; ambos trabajan en el hilo principal Qt.
    std::function<void(const QString&)> on_address_submitted_;

    // Acción que la sesión proporcionará para solicitar el retroceso, sin exponer CEF a la interfaz.
    std::function<void()> on_back_requested_;

    // Acción para avanzar en el historial; se ejecuta únicamente al activar la flecha Adelante.
    std::function<void()> on_forward_requested_;

    // Acción para recargar la página; se ejecuta únicamente al activar el botón Recargar.
    std::function<void()> on_reload_requested_;
};

}  // namespace sbp
