#include "cef_adapter_test_app.h"

#include "browser/browser_security_config.h"

#include "include/cef_command_line.h"

namespace sbp {

void CefAdapterTestApp::OnBeforeCommandLineProcessing(
    const CefString& processType, CefRefPtr<CefCommandLine> commandLine) {
    if (!processType.empty()) {
        return;
    }
    // La cobertura del historial debe utilizar la misma protección que la aplicación.
    BrowserSecurityConfig::Apply(commandLine);
    // El navegador conserva el dominio en la URL; solo su conexión se dirige al loopback.
    commandLine->AppendSwitchWithValue("host-resolver-rules", "MAP *.sbp.test 127.0.0.1");
    // El proxy del equipo no debe recibir ni resolver los dominios de este ejecutable de pruebas.
    commandLine->AppendSwitch("no-proxy-server");
}

}  // namespace sbp
