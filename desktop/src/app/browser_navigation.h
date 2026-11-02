#pragma once

#include "include/cef_base.h"

#include <functional>
#include <string>

namespace sbp {

class BrowserClient;
struct BrowserClientCallbacks;
class BrowserWindow;
class BrowserWindowUi;

// Coordina la navegación Qt↔CEF; cada control tiene su propio método privado.
// Los callbacks conservan sus dependencias explícitas; el estado referenciado pertenece a la sesión.
class BrowserNavigation final {
public:
    // Conecta los controles en Qt; sus acciones publicarán las operaciones para el hilo UI de CEF.
    void Connect(BrowserWindowUi& window_ui, CefRefPtr<BrowserClient> browser_client,
                 const bool& close_requested) const;

    // Agrupa los avisos de dirección e historial, conservando un único bloque de callbacks.
    BrowserClientCallbacks MakeCallbacks(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;

private:
    // Prepara los avisos CEF→Qt sin transferir referencias a objetos del navegador.
    std::function<void(std::string)> MakeAddressHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;
    std::function<void(bool)> MakeBackAvailabilityHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;

    // Cada método instala únicamente la acción correspondiente a su control Qt.
    void ConnectBackControl(BrowserWindowUi& window_ui,
                            const CefRefPtr<BrowserClient>& browser_client,
                            const bool& close_requested) const;
    void ConnectAddressControl(BrowserWindowUi& window_ui,
                               const CefRefPtr<BrowserClient>& browser_client,
                               const bool& close_requested) const;
};

}  // namespace sbp
