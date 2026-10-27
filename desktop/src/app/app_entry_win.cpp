#include "browser/browser_app.h"
#include "browser/browser_client.h"
#include "ui/browser_window.h"

#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"
#include "include/internal/cef_win.h"

#include <QApplication>
#include <QMetaObject>
#include <QThread>
#include <QVBoxLayout>
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

    // Este estado solo se consulta o modifica en el hilo Qt, aunque el aviso nazca en CEF.
    bool cef_context_ready = false;
    HWND browser_host_handle = nullptr;
    CefWindowInfo browser_window_info;
    bool browser_window_info_ready = false;

    // Podemos llegar por el aviso de CEF o por la creación del anfitrión, en cualquier orden.
    const auto prepare_browser_window_info = [&cef_context_ready, &browser_host_handle,
                                             &browser_window_info, &browser_window_info_ready] {
        if (!cef_context_ready || browser_host_handle == nullptr || browser_window_info_ready) {
            return;
        }

        // Medimos el área nativa en píxeles, desde el hilo Qt, para respetar el escalado de Windows.
        RECT host_bounds{};
        if (!GetClientRect(browser_host_handle, &host_bounds)
            || host_bounds.right <= 0 || host_bounds.bottom <= 0) {
            std::fputs("CEF window preparation failed: native host area unavailable.\n", stderr);
            return;
        }

        // SetAsChild solo rellena los parámetros: todavía no crea ninguna ventana ni navegador CEF.
        browser_window_info.SetAsChild(browser_host_handle,
            CefRect(0, 0, static_cast<int>(host_bounds.right), static_cast<int>(host_bounds.bottom)));
        browser_window_info.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
        browser_window_info.windowless_rendering_enabled = false;
        // Impide preparar dos veces la misma solicitud cuando ambas condiciones ya se cumplen.
        browser_window_info_ready = true;
        std::fprintf(stderr,
                     "CEF child window parameters prepared: parent %p; size %dx%d; runtime style %d; Qt main thread: %d.\n",
                     static_cast<void*>(browser_window_info.parent_window),
                     browser_window_info.bounds.width, browser_window_info.bounds.height,
                     static_cast<int>(browser_window_info.runtime_style), QThread::isMainThread());
    };

    // La referencia local vive hasta salir de RunWinMain; CEF puede conservar sus propias referencias.
    // Las variables capturadas permanecen vivas hasta después de CefShutdown y de liberar cef_app.
    CefRefPtr<sbp::BrowserApp> cef_app(new sbp::BrowserApp([&qt_app, &cef_context_ready,
                                                       &prepare_browser_window_info] {
        // CEF ejecuta esta función en su hilo UI. invokeMethod publica el trabajo en la cola de Qt.
        const bool queued = QMetaObject::invokeMethod(&qt_app, [&cef_context_ready,
                                                              &prepare_browser_window_info] {
            // Esta función interior se ejecuta al procesar eventos en el hilo principal de Qt.
            std::fprintf(stderr, "CEF context notification received; Qt main thread: %d.\n",
                         QThread::isMainThread());
            cef_context_ready = true;
            prepare_browser_window_info();
        }, Qt::QueuedConnection);
        // Comprobamos que Qt ha aceptado el envío; no bloqueamos el hilo CEF esperando la recepción.
        if (!queued) {
            std::fputs("CEF context notification could not be queued to Qt.\n", stderr);
        }
    }));

    // CefSettings contiene las opciones globales del motor. Conservamos el sandbox activado.
    CefSettings settings;
    settings.no_sandbox = false;
    // El contenido se alojará en una ventana hija nativa, sin renderizado fuera de pantalla.
    settings.windowless_rendering_enabled = false;
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
    // get() entrega el puntero sin abandonar la referencia que conserva cef_app.
    if (!CefInitialize(main_args, settings, cef_app.get(), sandbox_info)) {
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
        // La ventana principal contendrá un widget hijo reservado al contenido web.
        sbp::BrowserWindow window;
        // Esta función puede recibir el aviso de cierre CEF y trasladarlo a la propia ventana Qt.
        // La ventana debe seguir viva hasta recibir la confirmación; su closeEvent la conserva.
        const auto confirm_close_on_qt = [&window, &browser_host_handle] {
            const bool queued = QMetaObject::invokeMethod(&window, [&window, &browser_host_handle] {
                // El receptor es window, que sigue vivo; no se muestra ningún diálogo al usuario.
                std::fprintf(stderr, "Qt close confirmed; native host valid: %d.\n",
                             IsWindow(browser_host_handle) != FALSE);
                window.ConfirmClose();
            }, Qt::QueuedConnection);
            if (!queued) {
                std::fputs("Qt close confirmation could not be queued.\n", stderr);
            }
        };

        // El cliente no conserva CefBrowser: sus funciones notifican a Qt sin transferir esa referencia.
        CefRefPtr<sbp::BrowserClient> browser_client(new sbp::BrowserClient(
            [&window](int browser_id) {
                // CEF invoca esta función en su hilo UI; el identificador se copia a la cola Qt.
                const bool queued = QMetaObject::invokeMethod(&window, [browser_id] {
                    std::fprintf(stderr, "Qt received browser creation: id %d; Qt main thread: %d.\n",
                                 browser_id, QThread::isMainThread());
                }, Qt::QueuedConnection);
                if (!queued) {
                    std::fputs("CEF browser creation notification could not be queued to Qt.\n", stderr);
                }
            },
            confirm_close_on_qt));
        std::fputs("CEF browser client prepared; no browser instance created.\n", stderr);

        // Aún no hay navegador: la solicitud usa directamente la misma confirmación encolada.
        // Al crearlo, solicitaremos su cierre y esperaremos OnBeforeClose en lugar de confirmar aquí.
        window.SetCloseRequestHandler(confirm_close_on_qt);
        window.setWindowTitle("SecureBrowser");
        window.resize(1024, 768);

        // El layout distribuye el espacio interior; sin márgenes, el anfitrión ocupa toda el área.
        QVBoxLayout window_layout(&window);
        window_layout.setContentsMargins(0, 0, 0, 0);

        // El padre es window. El atributo exige una ventana nativa propia para alojar CEF después.
        QWidget browser_host(&window);
        browser_host.setAttribute(Qt::WA_NativeWindow);
        window_layout.addWidget(&browser_host);
        window.show();

        // winId() se consulta en el hilo Qt; en Windows identifica la ventana mediante un HWND.
        // Solo registramos ese identificador: todavía no creamos ni asociamos un CefBrowser.
        browser_host_handle = reinterpret_cast<HWND>(browser_host.winId());
        std::fprintf(stderr, "Qt native browser host ready: %p.\n",
                     static_cast<void*>(browser_host_handle));
        // Si el aviso CEF ya se procesó, esta llamada completa la preparación; si no, lo hará el aviso.
        prepare_browser_window_info();

        // Qt espera y despacha eventos en el principal; CEF mantiene su bucle en su propio hilo.
        // Al cerrar la última ventana, exec() termina y conserva el código de salida de Qt.
        std::fputs("Qt event loop starting with CEF initialized.\n", stderr);
        qt_exit_code = qt_app.exec();

        // Retiramos el identificador y los parámetros antes de destruir los widgets anfitriones.
        browser_host_handle = nullptr;
        browser_window_info = CefWindowInfo();
        browser_window_info_ready = false;
    }
    // Al salir del bloque se destruye la ventana, antes de apagar el motor.
    // Esta ventana aún no contiene un CefBrowser; al incrustarlo coordinaremos su cierre primero.
    std::fprintf(stderr, "Qt window destroyed; event loop exit code %d.\n", qt_exit_code);
    // Solo apagamos el motor tras una inicialización correcta, desde el mismo hilo principal.
    CefShutdown();
    std::fputs("CEF shutdown completed.\n", stderr);
    return qt_exit_code;
}
