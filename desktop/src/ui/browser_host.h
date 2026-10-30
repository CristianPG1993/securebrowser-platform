#pragma once

#include <QWidget>

#include <functional>

class QResizeEvent;
class QFocusEvent;

namespace sbp {

// Agrupa los avisos del anfitrión. Cada campo indica qué evento atiende la función guardada.
// Las funciones se ejecutan desde Qt; app_entry_win conecta sus avisos con las tareas CEF.
struct BrowserHostCallbacks {
    std::function<void()> on_resized;
    std::function<void()> on_focus_requested;
};

// Widget nativo que aloja la ventana hija CEF. Qt gestiona su tamaño mediante el layout.
class BrowserHost final : public QWidget {
public:
    // Recibe el bloque de avisos y el padre, sin añadir un argumento por cada nuevo evento.
    explicit BrowserHost(BrowserHostCallbacks callbacks, QWidget* parent);

protected:
    // Al volver de otra aplicación, Qt puede conservar su widget de foco sin emitir otro FocusIn.
    bool event(QEvent* event) override;

    // Qt llama a este método cuando el layout cambia el tamaño del anfitrión.
    void resizeEvent(QResizeEvent* event) override;

    // Qt avisa de que este widget recibe el foco; pedimos entregarlo a su contenido CEF.
    void focusInEvent(QFocusEvent* event) override;

private:
    // Conservamos las funciones durante la vida del anfitrión; recibirlas no las ejecuta.
    const BrowserHostCallbacks callbacks_;
};

}  // namespace sbp
