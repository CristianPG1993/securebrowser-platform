#pragma once

#include "include/cef_app.h"

namespace sbp {

// Configuración exclusiva del ejecutable de pruebas CEF para sus dominios HTTP locales.
class CefAdapterTestApp final : public CefApp {
public:
    // Se aplica antes de inicializar Chromium; solo se modifica el proceso navegador.
    void OnBeforeCommandLineProcessing(const CefString& processType,
                                       CefRefPtr<CefCommandLine> commandLine) override;

private:
    ~CefAdapterTestApp() override = default;

    IMPLEMENT_REFCOUNTING(CefAdapterTestApp);
};

}  // namespace sbp
