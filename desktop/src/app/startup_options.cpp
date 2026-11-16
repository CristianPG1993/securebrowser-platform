#include "app/startup_options.h"

#include <stdexcept>
#include <string>

namespace sbp {

// Conserva las opciones elegidas al arrancar; no crea contexto ni publica políticas.
StartupOptions::StartupOptions(bool urlFilteringDemo, DemoPolicyPreset initialDemoPolicy)
    : urlFilteringDemo_(urlFilteringDemo), initialDemoPolicy_(initialDemoPolicy) {}

StartupOptions StartupOptions::FromCommandLine(CefRefPtr<CefCommandLine> commandLine) {
    if (!commandLine) {
        throw std::invalid_argument("Startup options require a command line.");
    }

    const bool demoEnabled = commandLine->HasSwitch("demo-url-filtering");
    const bool hasDemoPolicy = commandLine->HasSwitch("demo-policy");
    if (!demoEnabled) {
        // Una política demo nunca activa por sí sola un modo de ejecución distinto.
        if (hasDemoPolicy) {
            throw std::invalid_argument("--demo-policy requires --demo-url-filtering.");
        }
        return StartupOptions(false, DemoPolicyPreset::FreeNavigation);
    }

    // El modo se activa por presencia, sin aceptar valores como true o false.
    if (!commandLine->GetSwitchValue("demo-url-filtering").empty()) {
        throw std::invalid_argument("--demo-url-filtering does not accept a value.");
    }

    DemoPolicyPreset initialPolicy = DemoPolicyPreset::FreeNavigation;
    if (hasDemoPolicy) {
        const std::string policy = commandLine->GetSwitchValue("demo-policy").ToString();
        if (policy == "restricted") {
            initialPolicy = DemoPolicyPreset::RestrictedNavigation;
        } else if (policy != "free") {
            // Una opción desconocida falla explícitamente; no selecciona la política libre.
            throw std::invalid_argument("--demo-policy must be free or restricted.");
        }
    }
    return StartupOptions(true, initialPolicy);
}

bool StartupOptions::IsUrlFilteringDemo() const noexcept {
    return urlFilteringDemo_;
}

DemoPolicyPreset StartupOptions::InitialDemoPolicy() const noexcept {
    return initialDemoPolicy_;
}

}  // namespace sbp
