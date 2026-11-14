#include "local_http_test_server.h"
#include "navigation_test_client.h"

#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

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

// Las reglas conservan el dominio que ve CEF; la resolución local no lo sustituye por una IP.
struct LocalHttpDomainCase {
    const char* name;
    const char* host;
    bool enabled;
    FilterMode mode;
    std::vector<std::string> rules;
    bool expectCancellation;
};

class LocalHttpDomainTest : public LocalHttpTestServerTest,
                            public ::testing::WithParamInterface<LocalHttpDomainCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<LocalHttpDomainCase>& info);
};

std::string LocalHttpDomainTest::CaseName(
    const ::testing::TestParamInfo<LocalHttpDomainCase>& info) {
    return info.param.name;
}

// Una respuesta del servidor demuestra la llegada; el bloqueo exige decisión y contador cero.
TEST_P(LocalHttpDomainTest, AppliesDomainPolicyAndChecksReceivedRequests) {
    const auto& testCase = GetParam();
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    const std::string path = testCase.expectCancellation ? "/blocked" : "/page";
    const std::string url = "http://" + std::string(testCase.host) + ":" + port + path;

    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Local domain test policy",
        UrlFilteringConfig(testCase.enabled, testCase.mode, testCase.rules),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager);
    CefRefPtr<NavigationTestClient> client = new NavigationTestClient(
        handler, url, testCase.expectCancellation);
    const auto result = client->Run();
    // Se cierra el servidor antes de leer el recuento final; TearDown admite repetir Stop.
    server_->Stop();

    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    ASSERT_TRUE(result.callbackObserved);
    EXPECT_FALSE(result.handlerCheckedDirectly);
    EXPECT_EQ(testCase.expectCancellation, result.cancelledBySecurity);
    EXPECT_EQ(!testCase.expectCancellation, result.documentLoaded);
    EXPECT_EQ(testCase.expectCancellation ? 0 : 200, result.documentStatusCode);
    EXPECT_EQ(testCase.expectCancellation ? 0U : 1U, server_->RequestCount(path));
}

// Dominios y subdominios se resuelven a loopback solo en CefAdapterTestApp.
INSTANTIATE_TEST_SUITE_P(
    LocalServer,
    LocalHttpDomainTest,
    ::testing::Values(
        LocalHttpDomainCase{"DenylistAllowsUnlisted", "allowed.sbp.test", true,
            FilterMode::Denylist, {"blocked.sbp.test"}, false},
        LocalHttpDomainCase{"DenylistBlocksListed", "blocked.sbp.test", true,
            FilterMode::Denylist, {"blocked.sbp.test"}, true},
        LocalHttpDomainCase{"AllowlistAllowsListed", "allowed.sbp.test", true,
            FilterMode::Allowlist, {"allowed.sbp.test"}, false},
        LocalHttpDomainCase{"AllowlistBlocksUnlisted", "blocked.sbp.test", true,
            FilterMode::Allowlist, {"allowed.sbp.test"}, true},
        LocalHttpDomainCase{"DenylistBlocksSubdomain", "child.blocked.sbp.test", true,
            FilterMode::Denylist, {"blocked.sbp.test"}, true},
        LocalHttpDomainCase{"AllowlistAllowsSubdomain", "child.allowed.sbp.test", true,
            FilterMode::Allowlist, {"allowed.sbp.test"}, false},
        LocalHttpDomainCase{"DenylistAllowsSimilarSuffix", "notblocked.sbp.test", true,
            FilterMode::Denylist, {"blocked.sbp.test"}, false},
        LocalHttpDomainCase{"AllowlistBlocksSimilarSuffix", "notallowed.sbp.test", true,
            FilterMode::Allowlist, {"allowed.sbp.test"}, true},
        LocalHttpDomainCase{"EmptyDenylistAllows", "allowed.sbp.test", true,
            FilterMode::Denylist, {}, false},
        LocalHttpDomainCase{"EmptyAllowlistBlocks", "allowed.sbp.test", true,
            FilterMode::Allowlist, {}, true},
        LocalHttpDomainCase{"DisabledDenylistAllows", "blocked.sbp.test", false,
            FilterMode::Denylist, {"blocked.sbp.test"}, false},
        LocalHttpDomainCase{"DisabledAllowlistAllows", "blocked.sbp.test", false,
            FilterMode::Allowlist, {"allowed.sbp.test"}, false},
        LocalHttpDomainCase{"DenylistNormalizesRule", "blocked.sbp.test", true,
            FilterMode::Denylist, {"BLOCKED.SBP.TEST."}, true},
        LocalHttpDomainCase{"AllowlistNormalizesRule", "allowed.sbp.test", true,
            FilterMode::Allowlist, {"ALLOWED.SBP.TEST."}, false}),
    LocalHttpDomainTest::CaseName);

}  // namespace sbp
