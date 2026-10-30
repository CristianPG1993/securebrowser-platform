#pragma once

#include <QWidget>

#include <functional>

class QResizeEvent;

namespace sbp {

// Widget nativo que aloja la ventana hija CEF. Qt gestiona su tamaño mediante el layout.
class BrowserHost final : public QWidget {
public:
    // Conserva el aviso que se ejecutará desde Qt cuando cambie el área disponible.
    explicit BrowserHost(std::function<void()> on_resized, QWidget* parent);

protected:
    // Qt llama a este método cuando el layout cambia el tamaño del anfitrión.
    void resizeEvent(QResizeEvent* event) override;

private:
    const std::function<void()> on_resized_;
};

}  // namespace sbp
