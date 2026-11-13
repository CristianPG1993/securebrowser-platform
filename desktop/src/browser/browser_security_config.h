#pragma once

#include "include/cef_command_line.h"

namespace sbp {

// Opciones de Chromium necesarias para que las navegaciones pasen por el control de seguridad.
class BrowserSecurityConfig final {
public:
    BrowserSecurityConfig() = delete;

    // Se invoca antes del arranque del proceso navegador, tanto en producción como en las pruebas.
    static void Apply(CefRefPtr<CefCommandLine> commandLine);
};

}  // namespace sbp
