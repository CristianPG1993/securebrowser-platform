#pragma once

#include "include/cef_server.h"

#include <cstddef>
#include <future>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace sbp {

// Servidor HTTP exclusivo de las pruebas, limitado a la interfaz IPv4 de loopback.
class LocalHttpTestServer final : public CefServerHandler {
public:
    LocalHttpTestServer();
    LocalHttpTestServer(const LocalHttpTestServer&) = delete;
    LocalHttpTestServer& operator=(const LocalHttpTestServer&) = delete;

    // Start y Stop se llaman desde el hilo de GoogleTest, nunca desde el hilo UI de CEF.
    // El objeto se utiliza una sola vez; Stop debe ejecutarse antes de CefShutdown.
    [[nodiscard]] bool Start();
    void Stop();

    // Copias protegidas para consultar desde GoogleTest lo publicado por el hilo servidor.
    [[nodiscard]] std::string Origin() const;
    [[nodiscard]] std::size_t RequestCount(const std::string& path) const;
    [[nodiscard]] std::size_t RequestMethodCount(const std::string& path, const std::string& method) const;

    // CEF ejecuta todos estos callbacks en el hilo dedicado al servidor.
    void OnServerCreated(CefRefPtr<CefServer> server) override;
    void OnServerDestroyed(CefRefPtr<CefServer> server) override;
    void OnClientConnected(CefRefPtr<CefServer> server, int connectionId) override;
    void OnClientDisconnected(CefRefPtr<CefServer> server, int connectionId) override;
    void OnHttpRequest(CefRefPtr<CefServer> server, int connectionId,
                       const CefString& clientAddress, CefRefPtr<CefRequest> request) override;
    void OnWebSocketRequest(CefRefPtr<CefServer> server, int connectionId,
                            const CefString& clientAddress, CefRefPtr<CefRequest> request,
                            CefRefPtr<CefCallback> callback) override;
    void OnWebSocketConnected(CefRefPtr<CefServer> server, int connectionId) override;
    void OnWebSocketMessage(CefRefPtr<CefServer> server, int connectionId,
                            const void* data, std::size_t dataSize) override;

private:
    ~LocalHttpTestServer() override = default;

    // Un timeout impide continuar con el apagado de CEF mientras queden recursos sin confirmar.
    [[noreturn]] static void AbortLifecycle(const char* message);

    // Sirve únicamente las páginas y el script del escenario de service worker, sin caché HTTP.
    [[nodiscard]] static bool SendServiceWorkerFixture(
        CefRefPtr<CefServer> server, int connectionId, const std::string& path);

    // Cadenas locales de dos redirecciones con códigos HTTP fijados por la prueba.
    [[nodiscard]] static bool SendRedirectFixture(
        CefRefPtr<CefServer> server, int connectionId, const std::string& path);

    // Documentos independientes para la página principal y su iframe.
    [[nodiscard]] static bool SendSubframeFixture(
        CefRefPtr<CefServer> server, int connectionId, const std::string& path);
    [[nodiscard]] static bool SendActionFixture(
        CefRefPtr<CefServer> server, int connectionId, const std::string& path, const std::string& method);

    bool startRequested_ = false;
    std::promise<bool> created_;
    std::promise<void> destroyed_;
    std::shared_future<void> destruction_;

    mutable std::mutex mutex_;
    CefRefPtr<CefServer> server_;
    std::string address_;
    std::map<std::string, std::size_t> requestCounts_;
    std::map<std::pair<std::string, std::string>, std::size_t> requestMethodCounts_;

    IMPLEMENT_REFCOUNTING(LocalHttpTestServer);
};

}  // namespace sbp
