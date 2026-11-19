#include "browser/support/local_http_test_server.h"
#include "browser/navigation/navigation_test_client.h"

#include "demo/demo_policy_source.h"
#include "include/cef_parser.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace sbp {

class DemoPolicyNavigationTest : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    CefRefPtr<LocalHttpTestServer> server_;
};

void DemoPolicyNavigationTest::SetUp() {
    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start()) << "Could not start the controlled demo HTTP server.";
}

void DemoPolicyNavigationTest::TearDown() {
    server_->Stop();
    server_ = nullptr;
}

struct DemoPolicyNavigationStep {
    const char* name;
    bool restricted;
    int expectedRequests;
};

// Utiliza los snapshots reales de la demo y un solo gestor/contexto, con navegadores de prueba independientes.
// El mismo destino responde en Libre, se cancela en Restrictiva y vuelve a responder al regresar a Libre.
TEST_F(DemoPolicyNavigationTest, SwitchingDemoPoliciesControlsActualHttpRequests) {
    CefURLParts endpoint;
    ASSERT_TRUE(CefParseURL(server_->Origin(), endpoint));
    const std::string port = CefString(&endpoint.port).ToString();
    ASSERT_FALSE(port.empty());
    // Solo el ejecutable de pruebas resuelve este dominio a loopback; la regla de la demo no cambia.
    // La ruta existente /action-target envía Cache-Control: no-store y no incluye recursos externos.
    const std::string path = "/action-target";
    const std::string url = "http://twitch.tv:" + port + path;

    const auto manager = std::make_shared<PolicyManager>(PolicyManager::Validity::Unlimited);
    const auto context = manager->BeginContext(DemoPolicySource::DeviceId, DemoPolicySource::CompanyId);
    ASSERT_TRUE(context.has_value());

    const DemoPolicyNavigationStep steps[] = {
        {"FreeBefore", false, 1},
        {"Restricted", true, 1},
        {"FreeAfter", false, 2}
    };
    RecordProperty("ControlledHost", "twitch.tv");
    for (const auto& step : steps) {
        SCOPED_TRACE(step.name);
        const auto policy = step.restricted ? DemoPolicySource::CreateRestrictedPolicy()
                                            : DemoPolicySource::CreateFreePolicy();
        ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));
        const auto received_before = server_->RequestCount(path);

        const auto notices = std::make_shared<std::vector<NavigationDecisionNotice>>();
        // CEF escribe en el vector y Run espera la finalización antes de que GoogleTest lo consulte.
        CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager,
            [notices](const NavigationDecisionNotice& notice) {
                notices->push_back(notice);
                return true;
            });
        CefRefPtr<NavigationTestClient> client = new NavigationTestClient(handler, url, step.restricted);
        const auto result = client->Run();

        ASSERT_TRUE(result.error.empty()) << result.error;
        ASSERT_TRUE(result.browserCreated);
        ASSERT_TRUE(result.browserClosed);
        ASSERT_TRUE(result.callbackObserved);
        EXPECT_FALSE(result.handlerCheckedDirectly);
        EXPECT_EQ(step.restricted, result.cancelledBySecurity);
        EXPECT_EQ(!step.restricted, result.documentLoaded);
        EXPECT_EQ(step.restricted ? 0 : 200, result.documentStatusCode);
        ASSERT_EQ(step.restricted ? 1U : 0U, notices->size());
        if (step.restricted) {
            const auto& notice = notices->front();
            EXPECT_EQ(SecurityAction::Block, notice.decision.Action());
            EXPECT_EQ(SecurityReason::DomainDenied, notice.decision.Reason());
            EXPECT_EQ("twitch.tv", notice.host);
            EXPECT_TRUE(notice.mainFrame);
            ASSERT_TRUE(notice.context.has_value());
            EXPECT_TRUE(*notice.context == *context);
            ASSERT_TRUE(notice.policyId.has_value());
            EXPECT_EQ(policy.Id(), *notice.policyId);
        }
        const auto received = server_->RequestCount(path);
        const auto new_requests = received - received_before;
        EXPECT_EQ(step.expectedRequests, static_cast<int>(received));
        EXPECT_EQ(step.restricted ? 0U : 1U, new_requests);
        RecordProperty(std::string(step.name) + "ReceivedRequests", static_cast<int>(received));
        RecordProperty(std::string(step.name) + "NewRequests", static_cast<int>(new_requests));
    }

    // Se espera la destrucción del servidor para que ninguna recepción pendiente altere la evidencia final.
    server_->Stop();
    EXPECT_EQ(2U, server_->RequestCount(path));
    RecordProperty("FinalReceivedRequests", static_cast<int>(server_->RequestCount(path)));
}

}  // namespace sbp
