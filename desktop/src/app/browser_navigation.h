#pragma once

#include "include/cef_base.h"

namespace sbp {

class BrowserClient;
class BrowserWindowUi;

// Conecta los controles de navegación en el hilo Qt y publica sus operaciones para CEF.
// close_requested pertenece a la sesión y debe vivir más que los callbacks de la interfaz.
void ConnectBrowserNavigation(BrowserWindowUi& window_ui,
                              CefRefPtr<BrowserClient> browser_client,
                              const bool& close_requested);

}  // namespace sbp
