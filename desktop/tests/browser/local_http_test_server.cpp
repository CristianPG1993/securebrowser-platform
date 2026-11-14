#include "local_http_test_server.h"

#include "include/cef_parser.h"
#include "include/cef_task.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string_view>

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

std::size_t LocalHttpTestServer::RequestMethodCount(const std::string& path, const std::string& method) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = requestMethodCounts_.find({path, method});
    return found == requestMethodCounts_.end() ? 0 : found->second;
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
        ++requestMethodCounts_[{path, request->GetMethod().ToString()}];
    }
    if (SendActionFixture(server, connectionId, path, request->GetMethod().ToString())) {
        return;
    }
    const bool historyPage = path == "/history-first" || path == "/history-second";
    if (request->GetMethod().ToString() == "GET"
        && SendSubframeFixture(server, connectionId, path)) {
        return;
    }
    if (request->GetMethod().ToString() == "GET"
        && SendRedirectFixture(server, connectionId, path)) {
        return;
    }
    if (request->GetMethod().ToString() == "GET"
        && SendServiceWorkerFixture(server, connectionId, path)) {
        return;
    }
    if (request->GetMethod().ToString() != "GET"
        || (path != "/page" && path != "/blocked" && !historyPage)) {
        server->SendHttp404Response(connectionId);
        return;
    }
    if (historyPage) {
        // pageshow.persisted distingue la restauración del documento de una nueva carga HTTP.
        // El marcador se consulta desde el navegador después de terminar el recorrido de vuelta.
        const char html[] = "<!doctype html><title>SBP history fixture</title>"
            "<link rel='icon' href='data:,'><p>Cacheable history document.</p>"
            "<script>window.sbpHistoryRestored=false;"
            "window.addEventListener('pageshow',function(event){"
            "window.sbpHistoryRestored=event.persisted;});</script>";
        const CefServer::HeaderMap headers{{"Cache-Control", "public, max-age=600"}};
        server->SendHttpResponse(connectionId, 200, "text/html; charset=utf-8",
                                 sizeof(html) - 1, headers);
        server->SendRawData(connectionId, html, sizeof(html) - 1);
        server->CloseConnection(connectionId);
        return;
    }
    // El contenido no carga recursos externos; /blocked también respondería si una petición escapara.
    const char html[] = "<!doctype html><title>SBP local HTTP test</title>"
                        "<link rel='icon' href='data:,'><p>Local navigation completed.</p>";
    server->SendHttp200Response(connectionId, "text/html; charset=utf-8", html, sizeof(html) - 1);
}

bool LocalHttpTestServer::SendActionFixture(
    CefRefPtr<CefServer> server, int connectionId, const std::string& path, const std::string& method) {
    const bool parent = path == "/action-parent" && method == "GET";
    const bool target = path == "/action-target" && (method == "GET" || method == "POST");
    if (!parent && !target) {
        return false;
    }
    const std::string html = parent
        ? "<!doctype html><title>SBP action parent</title><link rel='icon' href='data:,'><p>Action source.</p>"
        : "<!doctype html><title>SBP action target</title><link rel='icon' href='data:,'><p>Action completed.</p>";
    const CefServer::HeaderMap headers{{"Cache-Control", "no-store"}};
    server->SendHttpResponse(connectionId, 200, "text/html; charset=utf-8",
                             static_cast<std::int64_t>(html.size()), headers);
    server->SendRawData(connectionId, html.data(), html.size());
    server->CloseConnection(connectionId);
    return true;
}

bool LocalHttpTestServer::SendSubframeFixture(
    CefRefPtr<CefServer> server, int connectionId, const std::string& path) {
    if (path != "/frame-parent" && path != "/frame-child") {
        return false;
    }
    const std::string marker = path == "/frame-parent" ? "SBP parent fixture" : "SBP child fixture";
    const std::string html = "<!doctype html><title>" + marker
        + "</title><link rel='icon' href='data:,'><p>" + marker + "</p>";
    const CefServer::HeaderMap headers{{"Cache-Control", "no-store"}};
    server->SendHttpResponse(connectionId, 200, "text/html; charset=utf-8",
                             static_cast<std::int64_t>(html.size()), headers);
    server->SendRawData(connectionId, html.data(), html.size());
    server->CloseConnection(connectionId);
    return true;
}

bool LocalHttpTestServer::SendRedirectFixture(
    CefRefPtr<CefServer> server, int connectionId, const std::string& path) {
    if (path == "/redirect-target") {
        const char html[] = "<!doctype html><title>SBP redirect target</title>"
                            "<link rel='icon' href='data:,'><p>Redirect chain completed.</p>";
        const CefServer::HeaderMap headers{{"Cache-Control", "no-store"}};
        server->SendHttpResponse(connectionId, 200, "text/html; charset=utf-8",
                                 sizeof(html) - 1, headers);
        server->SendRawData(connectionId, html, sizeof(html) - 1);
        server->CloseConnection(connectionId);
        return true;
    }
    for (const int status : {301, 302, 303, 307, 308}) {
        const std::string prefix = "/redirect/" + std::to_string(status);
        const bool first = path == prefix + "/first";
        if (!first && path != prefix + "/second") {
            continue;
        }
        CefURLParts endpoint;
        if (!CefParseURL("http://" + server->GetAddress().ToString(), endpoint)) {
            server->SendHttp500Response(connectionId, "Invalid redirect fixture endpoint.");
            return true;
        }
        const std::string port = CefString(&endpoint.port).ToString();
        const std::string location = first
            ? "http://middle.sbp.test:" + port + prefix + "/second"
            : "http://target.sbp.test:" + port + "/redirect-target";
        const CefServer::HeaderMap headers{
            {"Location", location}, {"Cache-Control", "no-store"}};
        // Una respuesta sin cuerpo se cierra automáticamente en CEF; no repetir CloseConnection.
        server->SendHttpResponse(connectionId, status, "text/plain", 0, headers);
        return true;
    }
    return false;
}

bool LocalHttpTestServer::SendServiceWorkerFixture(
    CefRefPtr<CefServer> server, int connectionId, const std::string& path) {
    std::string_view body;
    std::string contentType = "text/html; charset=utf-8";
    CefServer::HeaderMap headers{{"Cache-Control", "no-store"}};
    if (path == "/sw-start") {
        // El aviso de disponibilidad exige registro activo y control del documento inicial.
        body = R"SWSTART(<!doctype html><title>SBP SW start</title>
<link rel="icon" href="data:,"><p>Service worker setup.</p><script>
(async function() {
    const timer = setTimeout(function() {
        document.title = 'SBP SW startup error/TimeoutError';
    }, 10000);
    try {
        await navigator.serviceWorker.register('/sw-worker.js', {scope: '/', updateViaCache: 'none'});
        await navigator.serviceWorker.ready;
        if (!navigator.serviceWorker.controller) {
            await new Promise(function(resolve) {
                function controlled() {
                    if (navigator.serviceWorker.controller) {
                        navigator.serviceWorker.removeEventListener('controllerchange', controlled);
                        resolve();
                    }
                }
                navigator.serviceWorker.addEventListener('controllerchange', controlled);
                controlled();
            });
        }
        clearTimeout(timer);
        document.title = 'SBP SW ready';
    } catch (error) {
        clearTimeout(timer);
        document.title = 'SBP SW startup error/' + error.name;
    }
})();
</script>)SWSTART";
    } else if (path == "/sw-worker.js") {
        contentType = "text/javascript; charset=utf-8";
        headers.emplace("Service-Worker-Allowed", "/");
        // Se prepara una entrada de Cache Storage; solo las navegaciones a /sw-target
        // reciben la respuesta marcada, construida a partir del contenido guardado.
        body = R"SWWORKER(const cacheName = 'sbp-navigation-policy-v1';
self.addEventListener('install', function(event) {
    event.waitUntil(caches.open(cacheName).then(function(cache) {
        return cache.add('/sw-target');
    }).then(function() {
        return self.skipWaiting();
    }));
});
self.addEventListener('activate', function(event) {
    event.waitUntil(self.clients.claim());
});
self.addEventListener('fetch', function(event) {
    if (event.request.mode !== 'navigate' || new URL(event.request.url).pathname !== '/sw-target') {
        return;
    }
    event.respondWith(caches.open(cacheName).then(function(cache) {
        return cache.match('/sw-target');
    }).then(function(response) {
        if (!response) throw new Error('Missing navigation fixture in Cache Storage');
        return response.text();
    }).then(function(html) {
        return new Response(html.replace('SBP SW network target', 'SBP SW cached target'), {
            status: 200,
            headers: {'Content-Type': 'text/html; charset=utf-8', 'Cache-Control': 'no-store'}
        });
    }));
});)SWWORKER";
    } else if (path == "/sw-target") {
        body = R"SWTARGET(<!doctype html><title>SBP SW network target</title>
<link rel="icon" href="data:,"><p>Content seeded into Cache Storage.</p>)SWTARGET";
    } else {
        return false;
    }
    server->SendHttpResponse(connectionId, 200, contentType,
                             static_cast<std::int64_t>(body.size()), headers);
    server->SendRawData(connectionId, body.data(), body.size());
    server->CloseConnection(connectionId);
    return true;
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
