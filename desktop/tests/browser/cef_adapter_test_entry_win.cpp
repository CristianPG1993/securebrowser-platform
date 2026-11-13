#include "cef_adapter_test_app.h"
#include "include/cef_sandbox_win.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

// Símbolo del enlazador que identifica esta DLL, como requiere CefMainArgs en Windows.
extern "C" IMAGE_DOS_HEADER __ImageBase;

// Entrada de la DLL de pruebas, invocada por el bootstrap de consola del SDK CEF.
CEF_BOOTSTRAP_EXPORT int RunConsoleMain(int argc,
                                      char* argv[],
                                      void* sandbox_info,
                                      cef_version_info_t* /*version_info*/) {
    const CefMainArgs main_args(reinterpret_cast<HINSTANCE>(&__ImageBase));
    CefRefPtr<sbp::CefAdapterTestApp> app = new sbp::CefAdapterTestApp();
    const int subprocess_exit_code = CefExecuteProcess(main_args, app, sandbox_info);
    if (subprocess_exit_code >= 0) {
        return subprocess_exit_code;
    }

    if (sandbox_info == nullptr) {
        std::fputs("CEF adapter tests require the bootstrap sandbox context.\n", stderr);
        return 1;
    }

    // GoogleTest se prepara antes de inicializar CEF, siguiendo el orden del SDK.
    ::testing::InitGoogleTest(&argc, argv);
    if (GTEST_FLAG_GET(list_tests)) {
        // Enumerar los casos no ejecuta sus cuerpos ni necesita crear un perfil CEF.
        return RUN_ALL_TESTS();
    }

    // El lanzador crea el directorio y lo retira después de que este proceso haya terminado.
    std::size_t profile_length = 0;
    if (_wgetenv_s(&profile_length, nullptr, 0, L"SBP_CEF_ADAPTER_TEST_PROFILE") != 0
        || profile_length <= 1) {
        std::fputs("CEF adapter tests require the CTest launcher profile.\n", stderr);
        return 1;
    }
    std::wstring profile_value(profile_length, L'\0');
    if (_wgetenv_s(&profile_length, profile_value.data(), profile_value.size(),
                  L"SBP_CEF_ADAPTER_TEST_PROFILE") != 0) {
        std::fputs("Could not read the CEF adapter test profile.\n", stderr);
        return 1;
    }
    profile_value.resize(profile_length - 1);
    const std::filesystem::path profile(profile_value);
    std::error_code error;
    if (!profile.is_absolute() || !std::filesystem::is_directory(profile, error) || error) {
        std::fputs("The CEF adapter test profile must be an existing absolute directory.\n", stderr);
        return 1;
    }

    CefSettings settings;
    settings.no_sandbox = false;
    settings.multi_threaded_message_loop = true;
    CefString(&settings.root_cache_path) = profile.wstring();
    CefString(&settings.log_file) = (profile / "cef.log").wstring();
    if (!CefInitialize(main_args, settings, app, sandbox_info)) {
        const int initialization_exit_code = CefGetExitCode();
        std::fputs("CEF adapter test initialization failed; temporary diagnostics retained.\n", stderr);
        // Una salida anticipada sin ejecutar las pruebas tampoco acredita un resultado correcto.
        return initialization_exit_code != 0 ? initialization_exit_code : 1;
    }

    const int test_exit_code = RUN_ALL_TESTS();
    CefShutdown();

    // La descarga de la DLL y el cierre del proceso pueden liberar archivos después de este retorno.
    return test_exit_code;
}
