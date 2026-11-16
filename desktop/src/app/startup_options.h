#pragma once

#include "include/cef_command_line.h"

namespace sbp {

// Selecciona una configuración demo conocida; no contiene reglas ni evalúa dominios.
enum class DemoPolicyPreset { FreeNavigation, RestrictedNavigation };

// Opciones propias de la aplicación, fijadas al arrancar el proceso principal.
// El modo demo es explícito y no representa una sesión autenticada del backend.
class StartupOptions final {
public:
    // Se consultará después de distinguir los subprocesos, antes de inicializar CEF.
    // Solo --demo-url-filtering sin valor activa la demostración; sin él, el modo es normal.
    // --demo-policy=free|restricted elige la política inicial; el valor predeterminado es free.
    // Rechaza una línea nula, valores no admitidos o --demo-policy sin activar el modo demo.
    [[nodiscard]] static StartupOptions FromCommandLine(CefRefPtr<CefCommandLine> commandLine);

    // Permite decidir la composición de la sesión; no concede permisos de navegación.
    [[nodiscard]] bool IsUrlFilteringDemo() const noexcept;

    // Solo se utiliza al componer una sesión demo; el modo normal no carga estas políticas.
    [[nodiscard]] DemoPolicyPreset InitialDemoPolicy() const noexcept;

private:
    // Solo la lectura explícita de las opciones puede seleccionar el modo inicial.
    StartupOptions(bool urlFilteringDemo, DemoPolicyPreset initialDemoPolicy);

    // Se conserva durante la sesión: no se cambia de modo mientras el navegador está abierto.
    const bool urlFilteringDemo_;

    // Es la elección al arrancar, no el estado de la política vigente del gestor.
    const DemoPolicyPreset initialDemoPolicy_;
};

}  // namespace sbp
