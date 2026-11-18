#pragma once

#include "browser/security_download_handler.h"

#include "include/cef_client.h"
#include "include/cef_download_handler.h"
#include "include/cef_life_span_handler.h"
#include "include/cef_load_handler.h"

#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <string>

namespace sbp {

// Permite comprobar tanto un enlace del documento como StartDownload, que omite CanDownload.
enum class DownloadTestTrigger { Link, StartDownload };

// Copias propias que GoogleTest consulta únicamente después de confirmar el cierre del navegador.
struct DownloadTestResult {
    bool browserCreated = false;
    bool browserClosed = false;
    std::size_t canDownloadCalls = 0;
    std::size_t beforeDownloadCalls = 0;
    std::size_t downloadUpdateCalls = 0;
    bool rejectedByCanDownload = false;
    bool completed = false;
    bool canceled = false;
    bool interrupted = false;
    std::int64_t receivedBytes = 0;
    std::optional<std::uint32_t> downloadId;
    std::string suggestedName;
    std::string fullPath;
    std::string error;
};

// Observa una descarga real en CEF y delega su autorización en el handler de producción.
// La elección del destino se proporciona mediante el callback del handler, sin necesitar widgets Qt.
class DownloadTestClient final : public CefClient,
                                 public CefLifeSpanHandler,
                                 public CefDownloadHandler,
                                 public CefLoadHandler {
public:
    DownloadTestClient(CefRefPtr<SecurityDownloadHandler> handler,
                       std::string pageUrl, std::string downloadUrl,
                       DownloadTestTrigger trigger = DownloadTestTrigger::Link);
    DownloadTestClient(const DownloadTestClient&) = delete;
    DownloadTestClient& operator=(const DownloadTestClient&) = delete;

    // Se llama una sola vez desde GoogleTest; espera a que CEF complete el cierre antes de devolver copias.
    [[nodiscard]] DownloadTestResult Run();

    // Solicita cerrar desde UI CEF para probar el cierre durante el diálogo o una transferencia activa.
    void Close();

    // CEF obtiene los observadores del cliente de prueba, que registran y delegan los callbacks de descarga.
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override;
    CefRefPtr<CefDownloadHandler> GetDownloadHandler() override;
    CefRefPtr<CefLoadHandler> GetLoadHandler() override;
    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    bool CanDownload(CefRefPtr<CefBrowser> browser,
                     const CefString& url, const CefString& requestMethod) override;
    bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefDownloadItem> downloadItem,
                          const CefString& suggestedName,
                          CefRefPtr<CefBeforeDownloadCallback> callback) override;
    void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefDownloadItem> downloadItem,
                           CefRefPtr<CefDownloadItemCallback> callback) override;

    // El documento de origen debe terminar de cargar antes de activar el enlace o StartDownload.
    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                   int httpStatusCode) override;
    void OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                     ErrorCode errorCode, const CefString& errorText,
                     const CefString& failedUrl) override;

private:
    ~DownloadTestClient() override = default;

    // Estos métodos y los datos de observación solo se utilizan en el hilo UI de CEF.
    void CreateBrowser();
    void RequestDownload(CefRefPtr<CefBrowser> browser);
    void CheckDeadline();
    void RequestClose(CefRefPtr<CefBrowser> browser);
    void CloseBrowser(CefRefPtr<CefBrowser> browser);
    void Complete();

    const CefRefPtr<SecurityDownloadHandler> handler_;
    const std::string pageUrl_;
    const std::string downloadUrl_;
    const DownloadTestTrigger trigger_;

    // El ID permite obtener referencias temporales en UI CEF, sin conservar el navegador en el cliente.
    int browserId_ = -1;
    bool downloadRequested_ = false;
    bool closing_ = false;
    bool completionSent_ = false;
    DownloadTestResult result_;
    std::promise<DownloadTestResult> completion_;

    IMPLEMENT_REFCOUNTING(DownloadTestClient);
};

}  // namespace sbp
