#pragma once

#include "include/cef_browser.h"

#include <functional>

namespace sbp {

// Consulta solo el índice de la entrada actual; no conserva entradas ni direcciones del historial.
class BrowserHistoryVisitor final : public CefNavigationEntryVisitor {
public:
    explicit BrowserHistoryVisitor(std::function<void(bool)> on_back_available,
                                   std::function<void(bool)> on_forward_available);
    BrowserHistoryVisitor(const BrowserHistoryVisitor&) = delete;
    BrowserHistoryVisitor& operator=(const BrowserHistoryVisitor&) = delete;

    // CEF llama a Visit en su hilo UI con una instantánea del historial real.
    bool Visit(CefRefPtr<CefNavigationEntry> entry, bool current, int index, int total) override;

private:
    // La última referencia de CEF libera el visitante después de consultar la entrada.
    ~BrowserHistoryVisitor() override;

    // Cada aviso conserva su propia función; los destinatarios publicarán solo booleanos en Qt.
    const std::function<void(bool)> on_back_available_;
    const std::function<void(bool)> on_forward_available_;
    IMPLEMENT_REFCOUNTING(BrowserHistoryVisitor);
};

}  // namespace sbp
