#include "local_http_test_server.h"
#include "redirect_navigation_test_client.h"

#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace sbp {

enum class RedirectBlockStage { None, Middle, Target };
using RedirectNavigationCase = std::tuple<int, RedirectBlockStage>;

class RedirectNavigationTest : public ::testing::TestWithParam<RedirectNavigationCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<RedirectNavigationCase>& info);

protected:
    void SetUp() override;
    void TearDown() override;
    CefRefPtr<LocalHttpTestServer> server_;
};

std::string RedirectNavigationTest::CaseName(
    const ::testing::TestParamInfo<RedirectNavigationCase>& info) {
    const auto [status, stage] = info.param;
    const std::string suffix = stage == RedirectBlockStage::None ? "Allowed"
        : stage == RedirectBlockStage::Middle ? "BlockedAtMiddle" : "BlockedAtTarget";
    return "Http" + std::to_string(status) + suffix;
}

void RedirectNavigationTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}

void RedirectNavigationTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// Cada código se prueba con permiso completo y con un bloqueo en cada destino de la cadena.
TEST_P(RedirectNavigationTest, ChecksEachRedirectBeforeContactingItsDestination) {
    const auto [status, stage] = GetParam();
    const bool blockedAtMiddle = stage == RedirectBlockStage::Middle;
    const bool blockedAtTarget = stage == RedirectBlockStage::Target;
    const bool allowed = stage == RedirectBlockStage::None;
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());

    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    std::vector<std::string> rules;
    if (!allowed) {
        rules.emplace_back(blockedAtMiddle ? "middle.sbp.test" : "target.sbp.test");
    }
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Redirect test policy", UrlFilteringConfig(true, FilterMode::Denylist, rules),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    const auto notices = std::make_shared<std::vector<NavigationDecisionNotice>>();
    // Solo la cancelación UI entrega el aviso; la observación IO no vuelve a publicarlo.
    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager,
        [notices](const NavigationDecisionNotice& notice) {
            notices->push_back(notice);
            return true;
        });
    CefRefPtr<RedirectNavigationTestClient> client = new RedirectNavigationTestClient(handler, port, status);
    const auto result = client->Run();
    server_->Stop();

    std::ostringstream trace;
    for (const auto& entry : result.trace) {
        trace << entry << '\n';
    }
    SCOPED_TRACE(trace.str());
    RecordProperty("callback_trace", trace.str());
    RecordProperty("ui_callbacks", result.uiCallbacks);
    RecordProperty("ui_redirect_callbacks", result.uiRedirectCallbacks);
    RecordProperty("io_loads", result.ioLoads);
    RecordProperty("io_redirects", result.ioRedirects);
    RecordProperty("middle_blocked", static_cast<int>(result.middleBlocked));
    RecordProperty("target_blocked", static_cast<int>(result.targetBlocked));
    RecordProperty("target_loaded", static_cast<int>(result.targetLoaded));
    RecordProperty("notice_count", static_cast<int>(notices->size()));
    const std::string prefix = "/redirect/" + std::to_string(status);
    RecordProperty("first_requests", static_cast<int>(server_->RequestCount(prefix + "/first")));
    RecordProperty("middle_requests", static_cast<int>(server_->RequestCount(prefix + "/second")));
    RecordProperty("target_requests", static_cast<int>(server_->RequestCount("/redirect-target")));

    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    EXPECT_TRUE(result.middleCallbackObserved);
    EXPECT_EQ(blockedAtMiddle, result.middleBlocked);
    EXPECT_EQ(!blockedAtMiddle, result.targetCallbackObserved);
    EXPECT_EQ(blockedAtTarget, result.targetBlocked);
    EXPECT_EQ(allowed, result.targetLoaded);
    EXPECT_EQ(allowed ? 200 : 0, result.targetStatus);
    EXPECT_EQ(blockedAtMiddle ? 2 : 3, result.uiCallbacks);
    EXPECT_EQ(blockedAtMiddle ? 1 : 2, result.uiRedirectCallbacks);
    EXPECT_EQ(blockedAtMiddle ? 1 : 2, result.ioRedirects);
    EXPECT_EQ(blockedAtMiddle ? 1 : allowed ? 3 : 2, result.ioLoads);
    EXPECT_EQ(1U, server_->RequestCount(prefix + "/first"));
    EXPECT_EQ(blockedAtMiddle ? 0U : 1U, server_->RequestCount(prefix + "/second"));
    EXPECT_EQ(allowed ? 1U : 0U, server_->RequestCount("/redirect-target"));
    ASSERT_EQ(allowed ? 0U : 1U, notices->size());
    if (!allowed) {
        const auto& notice = notices->front();
        EXPECT_GT(notice.operationId, 0U);
        EXPECT_TRUE(notice.mainFrame);
        EXPECT_TRUE(notice.redirect);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        EXPECT_EQ(blockedAtMiddle ? "middle.sbp.test" : "target.sbp.test", notice.host);
        EXPECT_EQ(SecurityReason::DomainDenied, notice.decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(
    HttpChain,
    RedirectNavigationTest,
    ::testing::Combine(
        ::testing::Values(301, 302, 303, 307, 308),
        ::testing::Values(RedirectBlockStage::None, RedirectBlockStage::Middle, RedirectBlockStage::Target)),
    RedirectNavigationTest::CaseName);

}  // namespace sbp
