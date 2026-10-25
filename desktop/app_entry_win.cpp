#include "include/cef_sandbox_win.h"

// Entrada que el bootstrap llama al cargar nuestra DLL (Dynamic-Link Library).
CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE hInstance,
                                  LPWSTR lpCmdLine,
                                  int nCmdShow,
                                  void* sandbox_info,
                                  cef_version_info_t* version_info) {
    // Implementación mínima para comprobar el contrato de entrada.
    return 0;
}
