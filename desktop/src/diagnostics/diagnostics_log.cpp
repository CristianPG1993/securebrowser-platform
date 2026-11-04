#include "diagnostics/diagnostics_log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <system_error>

#include <windows.h>
#include <shlobj.h>

namespace sbp {

bool DiagnosticsLog::Initialize(std::wstring& error_message) {
    // Primero necesitamos una carpeta disponible y las rutas de los dos archivos.
    if (!PrepareLogDirectory(error_message)) {
        return false;
    }
    // El resultado final indica si stderr ha quedado conectado al archivo propio.
    return RedirectStandardError(error_message);
}

const std::wstring& DiagnosticsLog::ApplicationLogPath() const {
    // Entregamos la ruta guardada por referencia de solo lectura, sin copiar la cadena.
    return application_log_path_;
}

const std::wstring& DiagnosticsLog::CefLogPath() const {
    // El arranque utilizará esta ruta para configurar el archivo interno de CEF.
    return cef_log_path_;
}

bool DiagnosticsLog::PrepareLogDirectory(std::wstring& error_message) {
    error_message.clear();
    // Windows resuelve la carpeta del usuario actual, sin fijar su nombre ni la unidad.
    PWSTR local_app_data = nullptr;
    const HRESULT path_result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData, 0, nullptr, &local_app_data);
    if (FAILED(path_result) || local_app_data == nullptr) {
        CoTaskMemFree(local_app_data);
        error_message = L"No se ha podido localizar AppData\\Local. HRESULT: "
            + std::to_wstring(static_cast<unsigned long>(path_result));
        return false;
    }

    // Guardamos nuestra copia antes de liberar la memoria que pertenece a la API de Windows.
    const std::wstring local_app_data_path(local_app_data);
    CoTaskMemFree(local_app_data);
    const std::filesystem::path log_directory =
        std::filesystem::path(local_app_data_path) / L"SecureBrowser" / L"Logs";

    // Si la carpeta ya existe, no es un fallo; comprobamos el código de error de la operación.
    std::error_code directory_error;
    std::filesystem::create_directories(log_directory, directory_error);
    if (directory_error) {
        error_message = L"No se ha podido preparar la carpeta de logs. Código: "
            + std::to_wstring(directory_error.value());
        return false;
    }

    // La fecha en milisegundos y el PID distinguen los archivos de cada ejecución.
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::wstring execution_id = std::to_wstring(timestamp)
        + L"-" + std::to_wstring(GetCurrentProcessId());
    application_log_path_ = (log_directory / (L"desktop-" + execution_id + L".log")).wstring();
    cef_log_path_ = (log_directory / (L"cef-" + execution_id + L".log")).wstring();
    return true;
}

bool DiagnosticsLog::RedirectStandardError(std::wstring& error_message) {
    error_message.clear();
    // La ruta debe haberse preparado antes de intentar abrir el archivo.
    if (application_log_path_.empty()) {
        error_message = L"No se ha preparado la ruta del archivo de diagnóstico.";
        return false;
    }

    // Reabrimos stderr sobre el archivo propio; la ruta y el modo usan caracteres anchos.
    std::FILE* log_stream = nullptr;
    const errno_t open_result = _wfreopen_s(
        &log_stream, application_log_path_.c_str(), L"w", stderr);
    if (open_result != 0 || log_stream == nullptr) {
        error_message = L"No se ha podido abrir el archivo de diagnóstico. Código: "
            + std::to_wstring(open_result);
        return false;
    }

    // Escribimos cada mensaje sin acumularlo en el búfer de la CRT, para conservar el diagnóstico reciente.
    if (std::setvbuf(log_stream, nullptr, _IONBF, 0) != 0) {
        error_message = L"No se ha podido configurar la escritura inmediata del diagnóstico.";
        return false;
    }
    // La CRT mantiene stderr abierto después de salir de este método y lo cierra al terminar el proceso.
    return true;
}

}  // namespace sbp
