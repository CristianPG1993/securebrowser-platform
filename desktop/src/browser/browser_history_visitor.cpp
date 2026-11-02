#include "browser/browser_history_visitor.h"

#include "include/cef_task.h"
#include "include/wrapper/cef_helpers.h"

#include <utility>

namespace sbp {

BrowserHistoryVisitor::BrowserHistoryVisitor(std::function<void(bool)> on_back_available)
    : on_back_available_(std::move(on_back_available)) {}

BrowserHistoryVisitor::~BrowserHistoryVisitor() = default;

bool BrowserHistoryVisitor::Visit(CefRefPtr<CefNavigationEntry>, bool current, int index, int) {
    CEF_REQUIRE_UI_THREAD();
    // El índice 0 representa la primera página; un índice mayor permite solicitar el retroceso.
    if (current && on_back_available_) {
        on_back_available_(index > 0);
    }
    // GetNavigationEntries se solicita con current_only=true: basta consultar la entrada actual.
    return false;
}

}  // namespace sbp
