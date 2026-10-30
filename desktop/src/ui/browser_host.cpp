#include "browser_host.h"

#include <QResizeEvent>

#include <utility>

namespace sbp {

BrowserHost::BrowserHost(std::function<void()> on_resized, QWidget* parent)
    : QWidget(parent), on_resized_(std::move(on_resized)) {
    // El anfitrión necesita un HWND propio para contener la ventana nativa de CEF.
    setAttribute(Qt::WA_NativeWindow);
}

void BrowserHost::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // Solo avisamos: la función registrada publicará el trabajo en el hilo UI de CEF.
    if (on_resized_) {
        on_resized_();
    }
}

}  // namespace sbp
