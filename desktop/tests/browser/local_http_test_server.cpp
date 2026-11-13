#include "local_http_test_server.h"

#include "include/cef_parser.h"
#include "include/cef_task.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace sbp {

LocalHttpTestServer::LocalHttpTestServer()
    : destruction_(destroyed_.get_future().share()) {}

bool LocalHttpTestServer::Start() {
    if (CefCurrentlyOn(TID_UI) || startRequested_) {
        throw std::logic_error("Start requires the test thread and a new server instance.");
    }
    startRequested_ = true;
    auto creation = created_.get_future();
    // El puerto cero deja la asignación al sistema; GetAddress publica el puerto realmente abierto.
    CefServer::CreateServer("127.0.0.1", 0, 16, this);
    if (creation.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        AbortLifecycle("Local HTTP test server creation timed out.\n");
    }
    return creation.get();
}

void LocalHttpTestServer::Stop() {
    if (CefCurrentlyOn(TID_UI)) {
        throw std::logic_error("Stop must not block the CEF UI thread.");
    }
    if (!startRequested_) {
        return;
    }
    CefRefPtr<CefServer> server;
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        server = server_;
    }
    // Shutdown se invoca fuera del mutex; los callbacks de cierre necesitan el mismo bloqueo.
    if (server) {
        server->Shutdown();
    }
    if (destruction_.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
        AbortLifecycle("Local HTTP test server destruction timed out.\n");
    }
    destruction_.get();
}

std::string LocalHttpTestServer::Origin() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return "http://" + address_;
}

std::size_t LocalHttpTestServer::RequestCount(const std::string& path) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = requestCounts_.find(path);
    return found == requestCounts_.end() ? 0 : found->second;
}

void LocalHttpTestServer::OnServerCreated(CefRefPtr<CefServer> server) {
    const bool running = server->IsRunning();
    if (running) {
        const std::lock_guard<std::mutex> lock(mutex_);
        server_ = server;
        address_ = server->GetAddress().ToString();
    }
    created_.set_value(running);
}

void LocalHttpTestServer::OnServerDestroyed(CefRefPtr<CefServer> /*server*/) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        server_ = nullptr;
    }
    // CEF ya ha cerrado el socket; se liberan las referencias propias antes de avisar al test.
    destroyed_.set_value();
}

void LocalHttpTestServer::OnClientConnected(CefRefPtr<CefServer> /*server*/,
                                           int /*connectionId*/) {}

void LocalHttpTestServer::OnClientDisconnected(CefRefPtr<CefServer> /*server*/,
                                              int /*connectionId*/) {}

void LocalHttpTestServer::OnHttpRequest(CefRefPtr<CefServer> server, int connectionId,
                                       const CefString& /*clientAddress*/,
                                       CefRefPtr<CefRequest> request) {
    CefURLParts parts;
    if (!CefParseURL(request->GetURL(), parts)) {
        server->SendHttp500Response(connectionId, "Invalid test request URL.");
        return;
    }
    const std::string path = CefString(&parts.path).ToString();
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        ++requestCounts_[path];
    }
    if (request->GetMethod().ToString() != "GET" || (path != "/page" && path != "/blocked")) {
        server->SendHttp404Response(connectionId);
        return;
    }
    // El contenido no carga recursos externos; /blocked también respondería si una petición escapara.
    const char html[] = "<!doctype html><title>SBP local HTTP test</title>"
                        "<link rel='icon' href='data:,'><p>Local navigation completed.</p>";
    server->SendHttp200Response(connectionId, "text/html; charset=utf-8", html, sizeof(html) - 1);
}

void LocalHttpTestServer::OnWebSocketRequest(CefRefPtr<CefServer> /*server*/,
                                            int /*connectionId*/,
                                            const CefString& /*clientAddress*/,
                                            CefRefPtr<CefRequest> /*request*/,
                                            CefRefPtr<CefCallback> callback) {
    callback->Cancel();
}

void LocalHttpTestServer::OnWebSocketConnected(CefRefPtr<CefServer> server, int connectionId) {
    server->CloseConnection(connectionId);
}

void LocalHttpTestServer::OnWebSocketMessage(CefRefPtr<CefServer> server, int connectionId,
                                            const void* /*data*/, std::size_t /*dataSize*/) {
    server->CloseConnection(connectionId);
}

void LocalHttpTestServer::AbortLifecycle(const char* message) {
    std::fputs(message, stderr);
    std::abort();
}

}  // namespace sbp
