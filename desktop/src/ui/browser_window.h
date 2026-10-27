#pragma once

#include <QWidget>

#include <functional>

// Aquí solo usamos un puntero; la definición del evento se incluye en el .cpp.
class QCloseEvent;

namespace sbp {

// Ventana principal Qt: mantiene visible el anfitrión mientras se coordina el cierre.
// Sus métodos y el callback de solicitud se utilizan únicamente desde el hilo Qt.
class BrowserWindow final : public QWidget {
public:
    BrowserWindow();

    // Se instala antes de mostrar la ventana; recibir la solicitud no equivale a confirmar el cierre.
    void SetCloseRequestHandler(std::function<void()> on_close_requested);

    // La confirmación interna permite un nuevo closeEvent, que esta vez se aceptará.
    void ConfirmClose();

protected:
    // Qt llama aquí al pulsar la X o al solicitar close() sobre esta ventana.
    void closeEvent(QCloseEvent* event) override;

private:
    std::function<void()> on_close_requested_;
    bool close_requested_ = false;
    bool close_confirmed_ = false;
};

}  // namespace sbp
