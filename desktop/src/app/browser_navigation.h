#pragma once

#include "include/cef_base.h"

#include <functional>
#include <string>

class QString;

namespace sbp {

class BrowserClient;
struct BrowserClientCallbacks;
class BrowserWindow;
class BrowserWindowUi;

// Coordina la navegación Qt↔CEF; cada control tiene su propio método privado.
// Los callbacks conservan sus dependencias explícitas; el estado referenciado pertenece a la sesión.
class BrowserNavigation final {
public:
    // Conecta los controles en Qt; sus acciones publicarán las operaciones para el hilo UI de CEF.
    void Connect(BrowserWindowUi& window_ui, CefRefPtr<BrowserClient> browser_client,
                 const bool& close_requested) const;

    // Agrupa los avisos de dirección e historial, conservando un único bloque de callbacks.
    BrowserClientCallbacks MakeCallbacks(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;

private:
    // Reconoce una autoridad host:puerto sin esquema mediante el mismo análisis estricto de QUrl.
    static bool IsHostWithPort(const QString& address);

    // Prepara los avisos CEF→Qt sin transferir referencias a objetos del navegador.
    std::function<void(std::string)> MakeAddressHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;
    std::function<void(bool)> MakeBackAvailabilityHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;
    std::function<void(bool)> MakeForwardAvailabilityHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;
    std::function<void(bool)> MakeReloadAvailabilityHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui, const bool& close_requested) const;
    // Esta pregunta también debe llegar a Qt durante el cierre, por eso no filtra close_requested.
    std::function<void(std::function<void(bool)>)> MakePageLeaveRequestHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui) const;
    std::function<void()> MakePageLeaveResetHandler(
        BrowserWindow& window, BrowserWindowUi* const& window_ui) const;

    // Cada método instala únicamente la acción correspondiente a su control Qt.
    void ConnectBackControl(BrowserWindowUi& window_ui,
                            const CefRefPtr<BrowserClient>& browser_client,
                            const bool& close_requested) const;
    void ConnectForwardControl(BrowserWindowUi& window_ui,
                               const CefRefPtr<BrowserClient>& browser_client,
                               const bool& close_requested) const;
    void ConnectAddressControl(BrowserWindowUi& window_ui,
                               const CefRefPtr<BrowserClient>& browser_client,
                               const bool& close_requested) const;
    void ConnectReloadControl(BrowserWindowUi& window_ui,
                              const CefRefPtr<BrowserClient>& browser_client,
                              const bool& close_requested) const;
};

}  // namespace sbp
