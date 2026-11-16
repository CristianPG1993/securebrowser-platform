#include "document_policy_test_client.h"
#include "local_http_test_server.h"

#include "demo/demo_policy_source.h"
#include "include/cef_parser.h"

#include <gtest/gtest.h>

namespace sbp {

struct DocumentPolicyChangeCase {
    const char* name;
    DocumentPolicyTestAction action;
    bool restricted;
    bool useJavaScript = false;
};

class DocumentPolicyChangeTest : public ::testing::TestWithParam<DocumentPolicyChangeCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<DocumentPolicyChangeCase>& info);
protected:
    void SetUp() override;
    void TearDown() override;
    CefRefPtr<LocalHttpTestServer> server_;
};

std::string DocumentPolicyChangeTest::CaseName(const ::testing::TestParamInfo<DocumentPolicyChangeCase>& info) {
    return info.param.name;
}

void DocumentPolicyChangeTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}

void DocumentPolicyChangeTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// La página inicial y la entrada de historial comparten documento; se observa BrowserClient real.
TEST_P(DocumentPolicyChangeTest, EnforcesUpdatedPolicyForExistingDocumentAndSameDocumentHistory) {
    const auto& testCase = GetParam();
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string origin = "http://twitch.tv:" + CefString(&endpoint.port).ToString();
    const auto manager = std::make_shared<PolicyManager>(PolicyManager::Validity::Unlimited);
    const auto context = manager->BeginContext(DemoPolicySource::DeviceId, DemoPolicySource::CompanyId);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, DemoPolicySource::CreateFreePolicy()));
    CefRefPtr<DocumentPolicyTestClient> client = new DocumentPolicyTestClient(
        manager, *context, origin + "/action-parent", origin + "/action-target",
        testCase.action, testCase.restricted, testCase.useJavaScript);
    const auto result = client->Run();
    server_->Stop();

    RecordProperty("same_document_entry_created", static_cast<int>(result.sameDocumentEntryCreated));
    RecordProperty("entry_created_with_user_activation", static_cast<int>(result.entryCreatedWithUserActivation));
    RecordProperty("neutral_document_loaded", static_cast<int>(result.neutralDocumentLoaded));
    RecordProperty("original_document_preserved", static_cast<int>(result.originalDocumentPreserved));
    RecordProperty("blocked_notices", static_cast<int>(result.notices.size()));
    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    ASSERT_TRUE(result.initialDocumentLoaded);
    ASSERT_TRUE(result.sameDocumentEntryCreated);
    ASSERT_TRUE(result.entryCreatedWithUserActivation);
    ASSERT_TRUE(result.updatedPolicyPublished);
    ASSERT_TRUE(result.actionRequested);
    ASSERT_TRUE(result.documentInspected);
    EXPECT_EQ(testCase.restricted, result.neutralDocumentLoaded);
    EXPECT_EQ(!testCase.restricted, result.originalDocumentPreserved);
    // El reemplazo neutro no debe emitir la señal que borraría el aviso de bloqueo en Qt.
    EXPECT_EQ(result.documentStartNoticesAtUpdate, result.documentStartNotices);
    EXPECT_EQ(1U, server_->RequestCount("/action-parent"));
    EXPECT_EQ(0U, server_->RequestCount("/action-target"));
    const bool revisit = testCase.action == DocumentPolicyTestAction::RevisitAfterRevocation;
    EXPECT_EQ(revisit, result.revokedHistoryBlocked);
    ASSERT_EQ(testCase.restricted ? (revisit ? 2U : 1U) : 0U, result.notices.size());
    for (const auto& notice : result.notices) {
        EXPECT_EQ(SecurityAction::Block, notice.decision.Action());
        EXPECT_EQ(SecurityReason::DomainDenied, notice.decision.Reason());
        EXPECT_EQ("twitch.tv", notice.host);
        EXPECT_TRUE(notice.mainFrame);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_TRUE(*notice.context == *context);
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(DemoPolicySource::CreateRestrictedPolicy().Id(), *notice.policyId);
    }
}

INSTANTIATE_TEST_SUITE_P(
    SameDocument,
    DocumentPolicyChangeTest,
    ::testing::Values(
        // El cambio explícito conserva una página permitida y retira una página revocada.
        DocumentPolicyChangeCase{"RevalidationStillAllowed", DocumentPolicyTestAction::Revalidate, false},
        DocumentPolicyChangeCase{"RevalidationNewlyBlocked", DocumentPolicyTestAction::Revalidate, true},
        // Atrás debe consultar la política incluso si no se reemplazó antes el documento.
        DocumentPolicyChangeCase{"CefBackStillAllowed", DocumentPolicyTestAction::Back, false},
        DocumentPolicyChangeCase{"CefBackNewlyBlocked", DocumentPolicyTestAction::Back, true},
        DocumentPolicyChangeCase{"JavaScriptBackStillAllowed", DocumentPolicyTestAction::Back, false, true},
        DocumentPolicyChangeCase{"JavaScriptBackNewlyBlocked", DocumentPolicyTestAction::Back, true, true},
        // Adelante se prepara retrocediendo primero con la política Libre.
        DocumentPolicyChangeCase{"CefForwardStillAllowed", DocumentPolicyTestAction::Forward, false},
        DocumentPolicyChangeCase{"CefForwardNewlyBlocked", DocumentPolicyTestAction::Forward, true},
        DocumentPolicyChangeCase{"JavaScriptForwardStillAllowed", DocumentPolicyTestAction::Forward, false, true},
        DocumentPolicyChangeCase{"JavaScriptForwardNewlyBlocked", DocumentPolicyTestAction::Forward, true, true},
        // Tras retirar el documento, una vuelta al historial tampoco puede volver a cargarlo.
        DocumentPolicyChangeCase{"BackAfterRevocationBlocked", DocumentPolicyTestAction::RevisitAfterRevocation, true}),
    DocumentPolicyChangeTest::CaseName);

}  // namespace sbp
