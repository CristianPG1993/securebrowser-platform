#include "browser/browser_client.h"
#include "browser/security_request_handler.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace sbp {

// El cliente exige un handler para evitar una conexión de seguridad inexistente.
TEST(BrowserClientTest, RejectsMissingRequestHandler) {
    const BrowserClientCallbacks callbacks{};
    const CefRefPtr<CefRequestHandler> missing_handler{};

    EXPECT_THROW({
        CefRefPtr<BrowserClient> client = new BrowserClient(callbacks, missing_handler);
    }, std::invalid_argument);
}

// El getter devuelve el objeto inyectado, sin crear ni sustituir el handler.
TEST(BrowserClientTest, ReturnsInjectedRequestHandler) {
    const auto manager = std::make_shared<PolicyManager>();
    CefRefPtr<SecurityRequestHandler> security_handler = new SecurityRequestHandler(manager);
    CefRefPtr<BrowserClient> client = new BrowserClient({}, security_handler);

    const CefRefPtr<CefRequestHandler> returned = client->GetRequestHandler();

    EXPECT_EQ(security_handler.get(), returned.get());
}

// El cliente y la referencia entregada a CEF conservan el handler y su gestor mientras se necesitan.
TEST(BrowserClientTest, PreservesHandlerLifetimeAcrossClientAndCefReferences) {
    auto manager = std::make_shared<PolicyManager>();
    const std::weak_ptr<PolicyManager> observer = manager;
    CefRefPtr<SecurityRequestHandler> security_handler = new SecurityRequestHandler(manager);
    CefRefPtr<BrowserClient> client = new BrowserClient({}, security_handler);

    manager.reset();
    security_handler = nullptr;
    EXPECT_FALSE(observer.expired());

    CefRefPtr<CefRequestHandler> cef_reference = client->GetRequestHandler();
    ASSERT_TRUE(cef_reference);
    client = nullptr;
    EXPECT_FALSE(observer.expired());

    cef_reference = nullptr;
    EXPECT_TRUE(observer.expired());
}

}  // namespace sbp
