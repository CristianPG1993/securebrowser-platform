#include "local_http_test_server.h"
#include "navigation_action_test_client.h"
#include "include/cef_parser.h"
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace sbp {

enum class ActionTargetKind { Http, Https, Data, Blob };
enum class NoticeReceiverMode { Accept, Reject, Throw };
struct NavigationActionCase {
    const char* name;
    NavigationTestAction action;
    bool blocked;
    ActionTargetKind target = ActionTargetKind::Http;
    int attempts = 1;
    NoticeReceiverMode receiver = NoticeReceiverMode::Accept;
};

class NavigationActionTest : public ::testing::TestWithParam<NavigationActionCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<NavigationActionCase>& info);
protected:
    void SetUp() override;
    void TearDown() override;
    CefRefPtr<LocalHttpTestServer> server_;
};

std::string NavigationActionTest::CaseName(const ::testing::TestParamInfo<NavigationActionCase>& info) {
    return info.param.name;
}
void NavigationActionTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start());
}
void NavigationActionTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

// La acción real inicia la navegación; el servidor acredita método y llegada del destino.
TEST_P(NavigationActionTest, EnforcesPolicyAndPublishesOnlyAppliedBlocks) {
    const auto& testCase = GetParam();
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    const std::string parentUrl = "http://actions.sbp.test:" + port + "/action-parent";
    const bool web = testCase.target == ActionTargetKind::Http || testCase.target == ActionTargetKind::Https;
    std::string targetUrl = (testCase.target == ActionTargetKind::Https ? "https://" : "http://")
        + std::string("target.sbp.test:") + port + "/action-target?private=fixture-token";
    if (testCase.target == ActionTargetKind::Data) { targetUrl = "data:text/plain,SBPfixture"; }
    if (testCase.target == ActionTargetKind::Blob) { targetUrl = "blob:"; }
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(1, 20, "Action test policy",
        UrlFilteringConfig(true, FilterMode::Denylist,
            testCase.blocked && web ? std::vector<std::string>{"target.sbp.test"} : std::vector<std::string>{}),
        DownloadControlConfig(false, FilterMode::Denylist, {}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));
    const auto notices = std::make_shared<std::vector<NavigationDecisionNotice>>();
    const auto receiverMode = testCase.receiver;
    // La captura se escribe en UI CEF y se consulta tras recibir la promesa de cierre.
    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager,
        [notices, receiverMode](const NavigationDecisionNotice& notice) {
            notices->push_back(notice);
            if (receiverMode == NoticeReceiverMode::Throw) {
                throw std::runtime_error("Controlled receiver failure");
            }
            return receiverMode == NoticeReceiverMode::Accept;
        });
    CefRefPtr<NavigationActionTestClient> client = new NavigationActionTestClient(
        handler, parentUrl, targetUrl, testCase.action, testCase.attempts);
    const auto result = client->Run();
    server_->Stop();
    RecordProperty("target_callbacks", result.targetCallbacks);
    RecordProperty("source_loads", result.sourceLoads);
    RecordProperty("blocked_attempts", result.blockedAttempts);
    RecordProperty("target_loaded", static_cast<int>(result.targetLoaded));
    RecordProperty("notice_count", static_cast<int>(notices->size()));
    RecordProperty("get_requests", static_cast<int>(server_->RequestMethodCount("/action-target", "GET")));
    RecordProperty("post_requests", static_cast<int>(server_->RequestMethodCount("/action-target", "POST")));
    ASSERT_TRUE(result.error.empty()) << result.error;
    EXPECT_TRUE(result.browserCreated);
    EXPECT_TRUE(result.browserClosed);
    EXPECT_TRUE(result.parentLoaded);
    EXPECT_EQ(testCase.attempts, result.targetCallbacks);
    EXPECT_EQ(testCase.blocked ? testCase.attempts : 0, result.blockedAttempts);
    EXPECT_EQ(!testCase.blocked, result.targetLoaded);
    EXPECT_EQ(testCase.blocked ? 0 : 200, result.targetStatus);
    EXPECT_EQ(testCase.attempts, result.sourceLoads);
    EXPECT_EQ(static_cast<std::size_t>(testCase.attempts), server_->RequestCount("/action-parent"));
    EXPECT_EQ(testCase.blocked ? 0U : 1U, server_->RequestCount("/action-target"));
    if (!testCase.blocked) {
        EXPECT_EQ(1U, server_->RequestMethodCount("/action-target",
            testCase.action == NavigationTestAction::FormPost ? "POST" : "GET"));
    }
    ASSERT_EQ(testCase.blocked ? static_cast<std::size_t>(testCase.attempts) : 0U, notices->size());
    std::uint64_t previousId = 0;
    for (const auto& notice : *notices) {
        EXPECT_GT(notice.operationId, previousId);
        EXPECT_GT(notice.occurredAt.time_since_epoch().count(), 0);
        previousId = notice.operationId;
        EXPECT_GT(notice.browserId, 0);
        EXPECT_FALSE(notice.frameId.empty());
        EXPECT_TRUE(notice.mainFrame);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(1, *notice.policyId);
        EXPECT_EQ(web ? "target.sbp.test" : "", notice.host);
        EXPECT_EQ(SecurityAction::Block, notice.decision.Action());
        EXPECT_EQ(web ? SecurityReason::DomainDenied : SecurityReason::UnsupportedScheme, notice.decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(Sources, NavigationActionTest, ::testing::Values(
    // LoadURL carga el destino permitido con HTTP 200 y no genera avisos de bloqueo.
    NavigationActionCase{"DirectAllowed", NavigationTestAction::Direct, false},
    // LoadURL intenta acceder al host de la denylist: DomainDenied, un aviso y ninguna petición al destino.
    NavigationActionCase{"DirectBlocked", NavigationTestAction::Direct, true},
    // Un enlace creado y activado en la página llega al destino mediante GET, sin avisos de bloqueo.
    NavigationActionCase{"LinkAllowed", NavigationTestAction::Link, false},
    // El enlace apunta al host de la denylist: se bloquea con DomainDenied antes de enviar la petición.
    NavigationActionCase{"LinkBlocked", NavigationTestAction::Link, true},
    // window.location.assign navega al destino permitido mediante GET, sin avisos de bloqueo.
    NavigationActionCase{"ScriptAllowed", NavigationTestAction::Script, false},
    // La navegación solicitada por JavaScript respeta la denylist y emite un aviso con DomainDenied.
    NavigationActionCase{"ScriptBlocked", NavigationTestAction::Script, true},
    // El formulario GET permitido llega al servidor y carga el destino con HTTP 200.
    NavigationActionCase{"FormGetAllowed", NavigationTestAction::FormGet, false},
    // El formulario GET hacia el host bloqueado se cancela antes de enviar la petición al destino.
    NavigationActionCase{"FormGetBlocked", NavigationTestAction::FormGet, true},
    // El formulario POST permitido llega al servidor como POST y carga el destino con HTTP 200.
    NavigationActionCase{"FormPostAllowed", NavigationTestAction::FormPost, false},
    // El formulario POST hacia el host bloqueado se cancela antes de enviar la petición al destino.
    NavigationActionCase{"FormPostBlocked", NavigationTestAction::FormPost, true},
    // Bloquea el destino HTTPS con DomainDenied y sin petición HTTP; no descarta conexiones TCP/TLS especulativas.
    NavigationActionCase{"HttpsBlockedBeforeHttpRequest", NavigationTestAction::Direct, true, ActionTargetKind::Https},
    // Dos intentos de navegar a la misma URL bloqueada generan dos avisos con identificadores distintos.
    NavigationActionCase{"RepeatedBlockedUrlHasDistinctNotices", NavigationTestAction::Direct, true, ActionTargetKind::Http, 2},
    // Aunque el receptor devuelva false al recibir el aviso, la navegación sigue bloqueada.
    NavigationActionCase{"ReceiverRejectionKeepsBlock", NavigationTestAction::Direct, true, ActionTargetKind::Http, 1, NoticeReceiverMode::Reject},
    // Aunque el receptor lance una excepción al recibir el aviso, la navegación sigue bloqueada.
    NavigationActionCase{"ReceiverExceptionKeepsBlock", NavigationTestAction::Direct, true, ActionTargetKind::Http, 1, NoticeReceiverMode::Throw},
    // Rechaza la navegación data: con UnsupportedScheme, aunque la denylist esté vacía.
    NavigationActionCase{"DataNavigationBlocked", NavigationTestAction::Direct, true, ActionTargetKind::Data},
    // Rechaza la URL blob: creada en la página con UnsupportedScheme, aunque la denylist esté vacía.
    NavigationActionCase{"BlobNavigationBlocked", NavigationTestAction::Blob, true, ActionTargetKind::Blob}),
    NavigationActionTest::CaseName);

}  // namespace sbp
