#include "local_http_test_server.h"
#include "subframe_navigation_test_client.h"

#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace sbp {

struct SubframeNavigationCase {
    const char* name;
    FilterMode mode;
    bool blockChild;
    int redirectStatus;
    const char* internalUrl;
};

class SubframeNavigationTest : public ::testing::TestWithParam<SubframeNavigationCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<SubframeNavigationCase>& info);

protected:
    void SetUp() override;
    void TearDown() override;
    CefRefPtr<LocalHttpTestServer> server_;
};

std::string SubframeNavigationTest::CaseName(
    const ::testing::TestParamInfo<SubframeNavigationCase>& info) {
    return info.param.name;
}

void SubframeNavigationTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}

void SubframeNavigationTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// El permiso del padre no autoriza automáticamente el documento cargado dentro de su iframe.
TEST_P(SubframeNavigationTest, AppliesPolicyToChildWithoutReplacingParentDocument) {
    const auto& testCase = GetParam();
    const bool internal = testCase.internalUrl != nullptr;
    const bool redirect = testCase.redirectStatus != 0;
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    const std::string parentUrl = "http://parent.sbp.test:" + port + "/frame-parent";
    const std::string prefix = "/redirect/" + std::to_string(testCase.redirectStatus);
    const std::string finalHost = redirect ? "target.sbp.test" : "child.sbp.test";
    std::string childUrl = "http://child.sbp.test:" + port + "/frame-child";
    std::string expectedChildUrl = childUrl;
    std::string marker = "SBP child fixture";
    if (redirect) {
        childUrl = "http://start.sbp.test:" + port + prefix + "/first";
        expectedChildUrl = "http://target.sbp.test:" + port + "/redirect-target";
        marker = "Redirect chain completed.";
    } else if (internal) {
        childUrl = testCase.internalUrl;
        expectedChildUrl = childUrl;
        marker = childUrl == "about:srcdoc" ? "SBP srcdoc fixture" : "";
    }

    std::vector<std::string> rules;
    if (testCase.mode == FilterMode::Allowlist) {
        rules.emplace_back("parent.sbp.test");
        if (redirect) {
            rules.emplace_back("start.sbp.test");
            rules.emplace_back("middle.sbp.test");
        }
        if (!testCase.blockChild && !internal) {
            rules.push_back(finalHost);
        }
    } else {
        rules.emplace_back(testCase.blockChild ? finalHost : "blocked.sbp.test");
    }
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Subframe test policy", UrlFilteringConfig(true, testCase.mode, rules),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    const auto notices = std::make_shared<std::vector<NavigationDecisionNotice>>();
    // La copia se entrega en UI y se consulta tras la promesa de cierre del navegador.
    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager,
        [notices](const NavigationDecisionNotice& notice) {
            notices->push_back(notice);
            return true;
        });
    CefRefPtr<SubframeNavigationTestClient> client = new SubframeNavigationTestClient(
        handler, parentUrl, childUrl, expectedChildUrl, marker);
    const auto result = client->Run();
    server_->Stop();

    std::ostringstream trace;
    for (const auto& entry : result.trace) {
        trace << entry << '\n';
    }
    SCOPED_TRACE(trace.str());
    RecordProperty("callback_trace", trace.str());
    RecordProperty("parent_loaded", static_cast<int>(result.parentLoaded));
    RecordProperty("parent_preserved", static_cast<int>(result.parentPreserved));
    RecordProperty("notice_count", static_cast<int>(notices->size()));
    RecordProperty("child_callback_observed", static_cast<int>(result.childCallbackObserved));
    RecordProperty("internal_handler_checked_directly", static_cast<int>(result.internalHandlerCheckedDirectly));
    RecordProperty("child_blocked", static_cast<int>(result.childBlocked));
    RecordProperty("child_loaded", static_cast<int>(result.childLoaded));
    RecordProperty("child_load_end_observed", static_cast<int>(result.childLoadEndObserved));
    RecordProperty("child_document_verified", static_cast<int>(result.childDocumentVerified));
    RecordProperty("child_web_callbacks", result.childWebCallbacks);
    RecordProperty("child_redirect_callbacks", result.childRedirectCallbacks);
    RecordProperty("parent_requests", static_cast<int>(server_->RequestCount("/frame-parent")));
    RecordProperty("child_requests", static_cast<int>(server_->RequestCount("/frame-child")));
    RecordProperty("redirect_first_requests", static_cast<int>(server_->RequestCount(prefix + "/first")));
    RecordProperty("redirect_middle_requests", static_cast<int>(server_->RequestCount(prefix + "/second")));
    RecordProperty("redirect_target_requests", static_cast<int>(server_->RequestCount("/redirect-target")));

    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    EXPECT_TRUE(result.parentCallbackObserved);
    EXPECT_TRUE(result.parentLoaded);
    EXPECT_TRUE(result.parentPreserved);
    EXPECT_TRUE(result.childStarted);
    EXPECT_EQ(testCase.blockChild, result.childBlocked);
    EXPECT_EQ(!testCase.blockChild, result.childLoaded);
    EXPECT_EQ(!testCase.blockChild, result.childDocumentVerified);
    if (internal) {
        // Una comprobación directa de la excepción interna no acredita un callback emitido por CEF.
        EXPECT_TRUE(result.childCallbackObserved || result.internalHandlerCheckedDirectly);
    } else {
        EXPECT_TRUE(result.childCallbackObserved);
        EXPECT_FALSE(result.internalHandlerCheckedDirectly);
        EXPECT_EQ(!testCase.blockChild, result.childLoadEndObserved);
        EXPECT_EQ(testCase.blockChild ? 0 : 200, result.childStatus);
    }
    EXPECT_EQ(internal ? 0 : redirect ? 3 : 1, result.childWebCallbacks);
    EXPECT_EQ(redirect ? 2 : 0, result.childRedirectCallbacks);
    EXPECT_EQ(1U, server_->RequestCount("/frame-parent"));
    EXPECT_EQ(!internal && !redirect && !testCase.blockChild ? 1U : 0U, server_->RequestCount("/frame-child"));
    EXPECT_EQ(redirect ? 1U : 0U, server_->RequestCount(prefix + "/first"));
    EXPECT_EQ(redirect ? 1U : 0U, server_->RequestCount(prefix + "/second"));
    EXPECT_EQ(redirect && !testCase.blockChild ? 1U : 0U, server_->RequestCount("/redirect-target"));
    ASSERT_EQ(testCase.blockChild ? 1U : 0U, notices->size());
    if (testCase.blockChild) {
        const auto& notice = notices->front();
        EXPECT_FALSE(notice.mainFrame);
        EXPECT_EQ(redirect, notice.redirect);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        EXPECT_EQ(finalHost, notice.host);
        EXPECT_EQ(testCase.mode == FilterMode::Allowlist ? SecurityReason::DomainNotAllowed
                                                       : SecurityReason::DomainDenied,
                  notice.decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(
    ChildFrame,
    SubframeNavigationTest,
    ::testing::Values(
        SubframeNavigationCase{"DenylistAllowsChild", FilterMode::Denylist, false, 0, nullptr},
        SubframeNavigationCase{"DenylistBlocksChild", FilterMode::Denylist, true, 0, nullptr},
        SubframeNavigationCase{"AllowlistAllowsChild", FilterMode::Allowlist, false, 0, nullptr},
        SubframeNavigationCase{"AllowlistBlocksChild", FilterMode::Allowlist, true, 0, nullptr},
        SubframeNavigationCase{"Redirect301Allowed", FilterMode::Denylist, false, 301, nullptr},
        SubframeNavigationCase{"Redirect301Blocked", FilterMode::Denylist, true, 301, nullptr},
        SubframeNavigationCase{"Redirect302Allowed", FilterMode::Denylist, false, 302, nullptr},
        SubframeNavigationCase{"Redirect302Blocked", FilterMode::Denylist, true, 302, nullptr},
        SubframeNavigationCase{"Redirect303Allowed", FilterMode::Denylist, false, 303, nullptr},
        SubframeNavigationCase{"Redirect303Blocked", FilterMode::Denylist, true, 303, nullptr},
        SubframeNavigationCase{"Redirect307Allowed", FilterMode::Denylist, false, 307, nullptr},
        SubframeNavigationCase{"Redirect307Blocked", FilterMode::Denylist, true, 307, nullptr},
        SubframeNavigationCase{"Redirect308Allowed", FilterMode::Denylist, false, 308, nullptr},
        SubframeNavigationCase{"Redirect308Blocked", FilterMode::Denylist, true, 308, nullptr},
        SubframeNavigationCase{"BlankWithDenylist", FilterMode::Denylist, false, 0, "about:blank"},
        SubframeNavigationCase{"BlankWithAllowlist", FilterMode::Allowlist, false, 0, "about:blank"},
        SubframeNavigationCase{"SrcdocWithDenylist", FilterMode::Denylist, false, 0, "about:srcdoc"},
        SubframeNavigationCase{"SrcdocWithAllowlist", FilterMode::Allowlist, false, 0, "about:srcdoc"}),
    SubframeNavigationTest::CaseName);

}  // namespace sbp
