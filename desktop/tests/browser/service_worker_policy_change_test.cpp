#include "local_http_test_server.h"
#include "service_worker_policy_test_client.h"

#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace sbp {

struct ServiceWorkerPolicyChangeCase {
    const char* name;
    bool blockTarget;
};

class ServiceWorkerPolicyChangeTest : public ::testing::TestWithParam<ServiceWorkerPolicyChangeCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<ServiceWorkerPolicyChangeCase>& info);

protected:
    void SetUp() override;
    void TearDown() override;

    CefRefPtr<LocalHttpTestServer> server_;
};

std::string ServiceWorkerPolicyChangeTest::CaseName(
    const ::testing::TestParamInfo<ServiceWorkerPolicyChangeCase>& info) {
    return info.param.name;
}

void ServiceWorkerPolicyChangeTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}

void ServiceWorkerPolicyChangeTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// El worker dispone del documento antes del cambio de política; la navegación debe reevaluarse igualmente.
TEST_P(ServiceWorkerPolicyChangeTest, RechecksPolicyBeforeShowingWorkerCachedNavigation) {
    const auto& testCase = GetParam();
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    const std::string origin = "http://localhost:" + port;
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot initialPolicy(
        1, 20, "Initial service worker policy", UrlFilteringConfig(true, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, initialPolicy));

    CefRefPtr<ServiceWorkerPolicyTestClient> client = new ServiceWorkerPolicyTestClient(
        manager, *context, origin, testCase.blockTarget);
    const auto result = client->Run();
    server_->Stop();

    RecordProperty("worker_ready", static_cast<int>(result.workerReady));
    RecordProperty("updated_policy_published", static_cast<int>(result.updatedPolicyPublished));
    RecordProperty("target_callback_observed", static_cast<int>(result.targetCallbackObserved));
    RecordProperty("target_blocked", static_cast<int>(result.targetBlocked));
    RecordProperty("target_loaded", static_cast<int>(result.targetLoaded));
    RecordProperty("served_from_worker_cache", static_cast<int>(result.servedFromWorkerCache));
    RecordProperty("cleanup_completed", static_cast<int>(result.cleanupCompleted));
    RecordProperty("target_http_requests", static_cast<int>(server_->RequestCount("/sw-target")));
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    ASSERT_TRUE(result.startupLoaded);
    ASSERT_TRUE(result.workerReady);
    ASSERT_TRUE(result.updatedPolicyPublished);
    ASSERT_TRUE(result.targetRequested);
    ASSERT_TRUE(result.targetCallbackObserved);
    EXPECT_EQ(testCase.blockTarget, result.targetBlocked);
    EXPECT_EQ(!testCase.blockTarget, result.targetLoaded);
    EXPECT_EQ(!testCase.blockTarget, result.servedFromWorkerCache);
    EXPECT_EQ(testCase.blockTarget ? 0 : 200, result.targetStatusCode);
    EXPECT_TRUE(result.cleanupCompleted);
    EXPECT_EQ(1U, server_->RequestCount("/sw-start"));
    EXPECT_GE(server_->RequestCount("/sw-worker.js"), 1U);
    // La única petición a /sw-target es la preparación de Cache Storage durante la instalación.
    EXPECT_EQ(1U, server_->RequestCount("/sw-target"));
}

INSTANTIATE_TEST_SUITE_P(
    PolicyUpdate,
    ServiceWorkerPolicyChangeTest,
    ::testing::Values(
        ServiceWorkerPolicyChangeCase{"CachedNavigationStillAllowed", false},
        ServiceWorkerPolicyChangeCase{"CachedNavigationNewlyBlocked", true}),
    ServiceWorkerPolicyChangeTest::CaseName);

}  // namespace sbp
