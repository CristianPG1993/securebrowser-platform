#include "local_http_test_server.h"
#include "navigation_test_client.h"

#include <gtest/gtest.h>

#include <memory>

namespace sbp {

// TearDown se ejecuta también cuando un ASSERT detiene el caso, para cerrar el servidor.
class LocalHttpTestServerTest : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    CefRefPtr<LocalHttpTestServer> server_;
};

void LocalHttpTestServerTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start()) << "Could not bind the local HTTP test server.";
}

void LocalHttpTestServerTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// Primera comprobación del servidor: HTTP real, callback de CEF, estado 200 y recepción contabilizada.
TEST_F(LocalHttpTestServerTest, ServesPageThroughCefAndRecordsRequest) {
    ASSERT_EQ(0, server_->Origin().find("http://127.0.0.1:"));
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Local HTTP test policy",
        // Este caso valida el servidor con IP literal; las reglas de dominio se probarán aparte.
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager);
    CefRefPtr<NavigationTestClient> client = new NavigationTestClient(
        handler, server_->Origin() + "/page", false);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    ASSERT_TRUE(result.callbackObserved);
    EXPECT_FALSE(result.handlerCheckedDirectly);
    EXPECT_FALSE(result.cancelledBySecurity);
    EXPECT_TRUE(result.documentLoaded);
    EXPECT_EQ(200, result.documentStatusCode);
    EXPECT_EQ(1U, server_->RequestCount("/page"));
}

}  // namespace sbp
