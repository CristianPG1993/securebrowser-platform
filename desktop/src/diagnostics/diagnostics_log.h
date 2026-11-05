#pragma once

#include <string>

namespace sbp {

// Prepara los archivos locales de diagnóstico de la aplicación y del motor CEF.
// Se inicializará una vez en el proceso principal, antes de crear la interfaz Qt.
class DiagnosticsLog final {
public:
    DiagnosticsLog() = default;
    DiagnosticsLog(const DiagnosticsLog&) = delete;
    DiagnosticsLog& operator=(const DiagnosticsLog&) = delete;

    // Prepara una carpeta escribible y redirige stderr al log propio de esta ejecución.
    // Si falla, devuelve false y entrega una explicación mediante error_message.
    bool Initialize(std::wstring& error_message);

    // Permite localizar el archivo que recibirá los diagnósticos propios de la aplicación.
    const std::wstring& ApplicationLogPath() const;

    // Entrega la ruta que el arranque configurará después en CefSettings::log_file.
    const std::wstring& CefLogPath() const;

private:
    // Localiza AppData\Local del usuario, crea la carpeta Logs y prepara las rutas de esta ejecución.
    bool PrepareLogDirectory(std::wstring& error_message);

    // Conecta el flujo stderr existente con el archivo propio para conservar los mensajes actuales.
    bool RedirectStandardError(std::wstring& error_message);

    // Registra configuración, PID y versiones de los SDK; comunica un fallo de escritura al arranque.
    bool WriteSessionHeader(std::wstring& error_message);

    // Ruta absoluta del log propio, conservada por el objeto mientras dure la ejecución.
    std::wstring application_log_path_;

    // Ruta absoluta independiente para los mensajes internos del motor CEF.
    std::wstring cef_log_path_;
};

}  // namespace sbp
