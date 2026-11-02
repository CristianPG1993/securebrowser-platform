#include "browser/browser_history_visitor.h"

#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <utility>

namespace sbp {

BrowserHistoryVisitor::BrowserHistoryVisitor(std::function<void(bool)> on_back_available,
                                           std::function<void(bool)> on_forward_available)
    : on_back_available_(std::move(on_back_available)),
      on_forward_available_(std::move(on_forward_available)) {}

BrowserHistoryVisitor::~BrowserHistoryVisitor() = default;

bool BrowserHistoryVisitor::Visit(CefRefPtr<CefNavigationEntry>, bool current, int index, int total) {
    CEF_REQUIRE_UI_THREAD();
    // Usamos la misma entrada actual para actualizar las dos direcciones del historial.
    if (current) {
        // El índice 0 representa la primera página; un índice mayor permite retroceder.
        if (on_back_available_) {
            on_back_available_(index > 0);
        }
        // Existe una página posterior cuando la posición siguiente sigue dentro del total.
        if (on_forward_available_) {
            on_forward_available_(index + 1 < total);
        }
    }
    // GetNavigationEntries se solicita con current_only=true: basta consultar la entrada actual.
    return false;
}

}  // namespace sbp
