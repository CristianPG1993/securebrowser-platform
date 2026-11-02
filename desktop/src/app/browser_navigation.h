#pragma once

#include "include/cef_base.h"

#include <functional>
#include <string>

namespace sbp {

class BrowserClient;
class BrowserWindow;
class BrowserWindowUi;

// Conecta los controles de navegación en el hilo Qt y publica sus operaciones para CEF.
// close_requested pertenece a la sesión y debe vivir más que los callbacks de la interfaz.
void ConnectBrowserNavigation(BrowserWindowUi& window_ui,
                              CefRefPtr<BrowserClient> browser_client,
                              const bool& close_requested);

// Prepara el aviso CEF→Qt. La ventana recibe la tarea; el puntero y el estado viven en la sesión.
// Se consulta el puntero por referencia porque los controles se conectan después y se retiran al terminar.
std::function<void(std::string)> MakeBrowserAddressHandler(
    BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested);

}  // namespace sbp
