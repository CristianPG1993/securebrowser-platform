#include "browser/security_request_handler.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

namespace sbp {

// El handler no se puede construir sin un gestor al que consultar la política.
TEST(SecurityRequestHandlerTest, RejectsMissingPolicyManager) {
    EXPECT_THROW({
        CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(nullptr);
    }, std::invalid_argument);
}

// El mismo handler compartido mantiene el gestor vivo hasta desaparecer su último CefRefPtr.
TEST(SecurityRequestHandlerTest, RetainsManagerUntilLastHandlerReferenceIsReleased) {
    auto manager = std::make_shared<PolicyManager>();
    const std::weak_ptr<PolicyManager> observer = manager;
    CefRefPtr<SecurityRequestHandler> first = new SecurityRequestHandler(manager);
    CefRefPtr<SecurityRequestHandler> second = first;

    manager.reset();
    EXPECT_FALSE(observer.expired());

    first = nullptr;
    EXPECT_FALSE(observer.expired());

    second = nullptr;
    EXPECT_TRUE(observer.expired());
}

}  // namespace sbp
