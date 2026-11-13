#include "browser/browser_security_config.h"

#include <sstream>
#include <stdexcept>
#include <string>

namespace sbp {

void BrowserSecurityConfig::Apply(CefRefPtr<CefCommandLine> commandLine) {
    if (!commandLine) {
        throw std::invalid_argument("Browser security configuration requires a command line.");
    }
    // BFCache puede restaurar documentos sin OnBeforeBrowse. Se fuerza su desactivación,
    // conservando las demás opciones y la caché HTTP que sí vuelve a evaluar la navegación.
    std::string disabledFeatures = "BackForwardCache";
    std::istringstream existing(commandLine->GetSwitchValue("disable-features").ToString());
    std::string entry;
    while (std::getline(existing, entry, ',')) {
        const auto first = entry.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            continue;
        }
        std::string name = entry.substr(first);
        if (name.front() == '*') {
            name.erase(0, 1);
        }
        name = name.substr(0, name.find_first_of("<: \t\r\n"));
        // Sustituye también las variantes con trial o estado predeterminado; la aplicación
        // no debe acabar dependiendo de una entrada previa que permita activar BFCache.
        if (name != "BackForwardCache") {
            disabledFeatures += "," + entry;
        }
    }
    commandLine->RemoveSwitch("disable-features");
    commandLine->AppendSwitchWithValue("disable-features", disabledFeatures);
}

}  // namespace sbp
