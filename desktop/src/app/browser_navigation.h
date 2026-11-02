#pragma once

#include "include/cef_base.h"

#include <functional>
#include <string>

namespace sbp {

class BrowserClient;
class BrowserWindow;
class BrowserWindowUi;

// Coordina la navegación Qt↔CEF; cada control tiene su propio método privado.
// Los callbacks conservan sus dependencias explícitas; el estado referenciado pertenece a la sesión.
class BrowserNavigation final {
public:
    // Conecta los controles en Qt; sus acciones publicarán las operaciones para el hilo UI de CEF.
    void Connect(BrowserWindowUi& window_ui, CefRefPtr<BrowserClient> browser_client,
                 const bool& close_requested) const;

    // Prepara el aviso CEF→Qt; el receptor es la ventana y el puntero se consulta por referencia.
    std::function<void(std::string)> MakeAddressHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;

private:
    // Cada método instala únicamente la acción correspondiente a su control Qt.
    void ConnectAddressControl(BrowserWindowUi& window_ui,
                               const CefRefPtr<BrowserClient>& browser_client,
                               const bool& close_requested) const;
};

}  // namespace sbp
