#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"

#include <QApplication>
#include <QWidget>

#include <shlobj.h>

#include <cstdio>
#include <string>

// Entrada que el bootstrap llama al cargar nuestra DLL (Dynamic-Link Library).
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                  LPWSTR lpCmdLine,
                                  int nCmdShow,
                                  void* sandbox_info,
                                  cef_version_info_t* version_info) {
    // Prepara los argumentos de CEF con la instancia de Windows recibida del bootstrap.
    CefMainArgs main_args(hInstance);

    // El mismo ejecutable sirve al proceso principal y a los subprocesos de CEF.
    // CEF consulta la línea de comandos y ejecuta el trabajo del subproceso si corresponde.
    // nullptr deja los callbacks de CefApp vacíos; el contexto de sandbox se pasa sin modificar.
    const int exit_code = CefExecuteProcess(main_args, nullptr, sandbox_info);
    if (exit_code >= 0) {
        // El subproceso ha terminado: devuelve su código sin crear objetos ni ventanas Qt.
        return exit_code;
    }

    // Solo el proceso principal llega aquí. El bootstrap debe aportar el contexto de aislamiento.
    if (sandbox_info == nullptr) {
        std::fputs("CEF startup failed: missing bootstrap sandbox context.\n", stderr);
        return 1;
    }

    // Qt necesita argumentos vivos hasta destruir QApplication; empezamos con el nombre del programa.
    int qt_argc = 1;
    char qt_program_name[] = "SecureBrowser";
    char* qt_argv[] = {qt_program_name, nullptr};
    // Solo el proceso principal crea Qt, antes de inicializar CEF y cualquier ventana.
    QApplication qt_app(qt_argc, qt_argv);
    std::fputs("Qt application created in the main process.\n", stderr);

    // CefSettings contiene las opciones globales del motor. Conservamos el sandbox activado.
    CefSettings settings;
    settings.no_sandbox = false;
    // CEF procesa sus eventos en un hilo propio, mientras Qt atiende la ventana en el principal.
    settings.multi_threaded_message_loop = true;
    settings.external_message_pump = false;

    // Windows obtiene la carpeta AppData\Local del usuario actual, sin fijar su nombre ni unidad.
    PWSTR local_app_data = nullptr;
    const HRESULT path_result = SHGetKnownFolderPath(
        FOLDERID_LocalAppData, 0, nullptr, &local_app_data);
    if (FAILED(path_result) || local_app_data == nullptr) {
        // La API exige liberar el resultado también si falla; acepta un puntero nulo.
        CoTaskMemFree(local_app_data);
        std::fputs("CEF startup failed: could not locate the local application data directory.\n", stderr);
        return 1;
    }

    // Copiamos la ruta antes de liberar la memoria que nos ha entregado Windows.
    const std::wstring root_cache_path = std::wstring(local_app_data) + L"\\SecureBrowser\\CEF";
    CoTaskMemFree(local_app_data);
    // CEF recibe su propia copia de la cadena y crea sus datos internos bajo esta raíz absoluta.
    CefString(&settings.root_cache_path) = root_cache_path;

    // Inicializa el motor una sola vez en este proceso, con el contexto recibido del bootstrap.
    if (!CefInitialize(main_args, settings, nullptr, sandbox_info)) {
        // CEF puede indicar un fallo o una salida anticipada normal: conservamos su código.
        // Esta rama termina sin llamar a CefShutdown, porque la inicialización no continúa.
        const int initialization_exit_code = CefGetExitCode();
        std::fprintf(stderr, "CEF initialization did not continue: exit code %d.\n",
                     initialization_exit_code);
        return initialization_exit_code;
    }

    std::fputs("CEF initialized successfully with bootstrap sandbox context.\n", stderr);

    int qt_exit_code = 0;
    // Estas llaves crean un ámbito local que limita la vida del objeto window.
    {
        // Reutilizamos la ventana mínima, ahora con CEF activo durante toda su vida.
        QWidget window;
        window.setWindowTitle("SecureBrowser");
        window.resize(1024, 768);
        window.show();

        // Qt espera y despacha eventos en el principal; CEF mantiene su bucle en su propio hilo.
        // Al cerrar la última ventana, exec() termina y conserva el código de salida de Qt.
        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);
        qt_exit_code = qt_app.exec();
    }
    // Al salir del bloque se destruye la ventana, antes de apagar el motor.
    // Esta ventana aún no contiene un CefBrowser; al incrustarlo coordinaremos su cierre primero.
    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    // Solo apagamos el motor tras una inicialización correcta, desde el mismo hilo principal.
    CefShutdown();
    std::fputs("CEF shutdown completed.\n", stderr);
    return qt_exit_code;
}
