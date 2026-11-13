#include "history_policy_test_client.h"
#include "local_http_test_server.h"

#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace sbp {

struct HistoryPolicyChangeCase {
    const char* name;
    bool useJavaScript;
    bool blockFirstPage;
};

class HistoryPolicyChangeTest : public ::testing::TestWithParam<HistoryPolicyChangeCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<HistoryPolicyChangeCase>& info);

protected:
    void SetUp() override;
    void TearDown() override;

    CefRefPtr<LocalHttpTestServer> server_;
};

std::string HistoryPolicyChangeTest::CaseName(
    const ::testing::TestParamInfo<HistoryPolicyChangeCase>& info) {
    return info.param.name;
}

void HistoryPolicyChangeTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}

void HistoryPolicyChangeTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// Los controles permitidos prueban que Atrás funciona; el cambio de política debe impedir el retorno.
TEST_P(HistoryPolicyChangeTest, RechecksPolicyWhenReturningToCachedPage) {
    const auto& testCase = GetParam();
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    const std::string firstUrl = "http://first.sbp.test:" + port + "/history-first";
    const std::string secondUrl = "http://second.sbp.test:" + port + "/history-second";
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot initialPolicy(
        1, 20, "Initial history policy", UrlFilteringConfig(true, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, initialPolicy));

    CefRefPtr<HistoryPolicyTestClient> client = new HistoryPolicyTestClient(
        manager, *context, firstUrl, secondUrl, testCase.useJavaScript, testCase.blockFirstPage);
    const auto result = client->Run();
    server_->Stop();

    // Las propiedades quedan en el XML para distinguir HTTP cache de una restauración BFCache.
    RecordProperty("back_callback_observed", static_cast<int>(result.backCallbackObserved));
    RecordProperty("restored_from_bfcache", static_cast<int>(result.restoredFromBfcache));
    RecordProperty("first_page_returned", static_cast<int>(result.firstPageReturned));
    RecordProperty("first_page_requests", static_cast<int>(server_->RequestCount("/history-first")));
    RecordProperty("first_page_http_status", result.firstPageStatusCode);
    RecordProperty("second_page_http_status", result.secondPageStatusCode);
    RecordProperty("return_load_status", result.returnLoadStatus);
    RecordProperty("updated_policy_published", static_cast<int>(result.updatedPolicyPublished));
    RecordProperty("back_requested", static_cast<int>(result.backRequested));
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    ASSERT_TRUE(result.firstPageLoaded);
    ASSERT_TRUE(result.secondPageLoaded);
    EXPECT_EQ(200, result.firstPageStatusCode);
    EXPECT_EQ(200, result.secondPageStatusCode);
    ASSERT_TRUE(result.updatedPolicyPublished);
    ASSERT_TRUE(result.backRequested);
    EXPECT_EQ(testCase.blockFirstPage, result.backBlocked)
        << "Back callback observed: " << result.backCallbackObserved
        << "; restored from BFCache: " << result.restoredFromBfcache;
    EXPECT_EQ(!testCase.blockFirstPage, result.firstPageReturned);
    EXPECT_EQ(!testCase.blockFirstPage, result.restoredDocumentInspected);
    EXPECT_EQ(1U, server_->RequestCount("/history-first"));
    EXPECT_EQ(1U, server_->RequestCount("/history-second"));
    // El arranque compartido desactiva BFCache: incluso un retorno permitido debe pasar
    // por el callback y puede aprovechar la caché HTTP sin restaurar el documento anterior.
    EXPECT_TRUE(result.backCallbackObserved);
    EXPECT_FALSE(result.restoredFromBfcache);
}

INSTANTIATE_TEST_SUITE_P(
    PolicyUpdate,
    HistoryPolicyChangeTest,
    ::testing::Values(
        HistoryPolicyChangeCase{"CefBackStillAllowed", false, false},
        HistoryPolicyChangeCase{"JavaScriptBackStillAllowed", true, false},
        HistoryPolicyChangeCase{"CefBackNewlyBlocked", false, true},
        HistoryPolicyChangeCase{"JavaScriptBackNewlyBlocked", true, true}),
    HistoryPolicyChangeTest::CaseName);

}  // namespace sbp
