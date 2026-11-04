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

    // Atiende los cambios de foco nativo del marco, aunque Qt conserve su estado lógico anterior.
    void SetNativeFocusHandler(std::function<void()> on_native_focus);

    // La confirmación interna permite un nuevo closeEvent, que esta vez se aceptará.
    void ConfirmClose();

    // Retira una solicitud pendiente cuando CEF cancela el cierre; se utiliza desde el hilo Qt.
    void CancelClose();

protected:
    // Qt llama aquí al pulsar la X o al solicitar close() sobre esta ventana.
    void closeEvent(QCloseEvent* event) override;

    // Observa WM_SETFOCUS y WM_KILLFOCUS sin impedir su procesamiento normal por Qt.
    bool nativeEvent(const QByteArray& event_type, void* message, qintptr* result) override;

private:
    std::function<void()> on_close_requested_;
    std::function<void()> on_native_focus_;
    bool close_requested_ = false;
    bool close_confirmed_ = false;
};

}  // namespace sbp
