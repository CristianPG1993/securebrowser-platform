#include "navigation_test_client.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace sbp {

// Cada estado se prepara únicamente en el gestor aislado del caso, sin modificar la aplicación.
enum class NavigationPolicyState { NoContext, NoPolicy, Available, Expired, Ended };

struct SecurityNavigationCase {
    const char* name;
    const char* url;
    NavigationPolicyState state;
    FilterMode mode;
    bool expectCancellation;
    bool expectBeforeBrowse;
};

class SecurityNavigationTest : public ::testing::TestWithParam<SecurityNavigationCase> {
public:
    // Nombres estables y legibles para el XML y los diagnósticos de GoogleTest.
    static std::string CaseName(const ::testing::TestParamInfo<SecurityNavigationCase>& info);
};

std::string SecurityNavigationTest::CaseName(
    const ::testing::TestParamInfo<SecurityNavigationCase>& info) {
    return info.param.name;
}

// Los casos web exigen el callback real. about:blank comprueba carga y permiso del handler por separado.
// El hilo de pruebas solo consulta el resultado tras el cierre del navegador.
TEST_P(SecurityNavigationTest, ChecksMainFrameNavigationAndHandlerDecision) {
    const auto& testCase = GetParam();
    const auto elapsed = std::make_shared<std::atomic<std::int64_t>>(0);
    // El reloj se comparte por valor y admite lecturas desde el hilo UI sin referencias locales colgantes.
    const auto manager = std::make_shared<PolicyManager>([elapsed] {
        return PolicyManager::Clock::time_point{} + std::chrono::minutes(elapsed->load());
    });
    if (testCase.state != NavigationPolicyState::NoContext) {
        const auto context = manager->BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());
        if (testCase.state != NavigationPolicyState::NoPolicy) {
            const PolicySnapshot::Timestamp timestamp{};
            const PolicySnapshot policy(
                1, 20, "Navigation test policy",
                UrlFilteringConfig(true, testCase.mode,
                    testCase.mode == FilterMode::Denylist
                        ? std::vector<std::string>{"blocked.example"}
                        : std::vector<std::string>{}),
                DownloadControlConfig(false, FilterMode::Denylist, {}),
                timestamp, timestamp);
            ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));
        }
        if (testCase.state == NavigationPolicyState::Expired) {
            elapsed->store(15);
        } else if (testCase.state == NavigationPolicyState::Ended) {
            ASSERT_TRUE(manager->EndContext(*context));
        }
    }

    const auto notices = std::make_shared<std::vector<NavigationDecisionNotice>>();
    // Se capturan los motivos generales sin mover objetos CEF al hilo de GoogleTest.
    CefRefPtr<SecurityRequestHandler> handler = new SecurityRequestHandler(manager,
        [notices](const NavigationDecisionNotice& notice) {
            notices->push_back(notice);
            return true;
        });
    CefRefPtr<NavigationTestClient> client = new NavigationTestClient(
        handler, testCase.url, testCase.expectCancellation);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_EQ(testCase.expectBeforeBrowse, result.callbackObserved);
    EXPECT_EQ(!testCase.expectBeforeBrowse, result.handlerCheckedDirectly);
    ASSERT_TRUE(result.callbackObserved || result.handlerCheckedDirectly);
    EXPECT_EQ(testCase.expectCancellation, result.cancelledBySecurity);
    EXPECT_EQ(!testCase.expectCancellation, result.documentLoaded);
    ASSERT_EQ(testCase.expectCancellation ? 1U : 0U, notices->size());
    if (testCase.expectCancellation) {
        const auto& notice = notices->front();
        EXPECT_GT(notice.operationId, 0U);
        EXPECT_TRUE(notice.mainFrame);
        SecurityReason reason = SecurityReason::DomainDenied;
        if (testCase.state == NavigationPolicyState::NoContext || testCase.state == NavigationPolicyState::Ended) {
            reason = SecurityReason::InvalidContext;
            EXPECT_FALSE(notice.context.has_value());
        } else if (testCase.state == NavigationPolicyState::NoPolicy) {
            reason = SecurityReason::PolicyUnavailable;
            EXPECT_FALSE(notice.policyId.has_value());
        } else if (testCase.state == NavigationPolicyState::Expired) {
            reason = SecurityReason::PolicyExpired;
        } else if (testCase.mode == FilterMode::Allowlist) {
            reason = SecurityReason::DomainNotAllowed;
        } else if (std::string(testCase.name) == "LiteralIp") {
            reason = SecurityReason::IpBlocked;
        }
        EXPECT_EQ(reason, notice.decision.Reason());
    }
}

// Los permisos de esta primera batería son internos; ninguna URL HTTP(S) se deja salir a la red.
INSTANTIATE_TEST_SUITE_P(
    MainFrame,
    SecurityNavigationTest,
    ::testing::Values(
        // Permite about:blank sin contexto por ser una navegación interna; no emite aviso de bloqueo.
        SecurityNavigationCase{"BlankWithoutContext", "about:blank",
            NavigationPolicyState::NoContext, FilterMode::Denylist, false, false},
        // Permite about:blank aunque la política haya caducado; no emite aviso de bloqueo.
        SecurityNavigationCase{"BlankWithExpiredPolicy", "about:blank",
            NavigationPolicyState::Expired, FilterMode::Denylist, false, false},
        // No crea contexto ni política: cancela la navegación HTTP con InvalidContext.
        SecurityNavigationCase{"HttpWithoutContext", "http://blocked.example/",
            NavigationPolicyState::NoContext, FilterMode::Denylist, true, true},
        // No crea contexto ni política: cancela la navegación HTTPS con InvalidContext.
        SecurityNavigationCase{"HttpsWithoutContext", "https://blocked.example/",
            NavigationPolicyState::NoContext, FilterMode::Denylist, true, true},
        // Crea el contexto sin publicar una política: cancela con PolicyUnavailable.
        SecurityNavigationCase{"ContextWithoutPolicy", "https://blocked.example/",
            NavigationPolicyState::NoPolicy, FilterMode::Denylist, true, true},
        // Publica la política y adelanta el reloj 15 minutos: cancela con PolicyExpired.
        SecurityNavigationCase{"ExpiredPolicy", "https://blocked.example/",
            NavigationPolicyState::Expired, FilterMode::Denylist, true, true},
        // Crea y después cierra el contexto: cancela con InvalidContext porque ya no hay estado activo.
        SecurityNavigationCase{"EndedContext", "https://blocked.example/",
            NavigationPolicyState::Ended, FilterMode::Denylist, true, true},
        // La política está disponible, pero blocked.example figura en la denylist: cancela con DomainDenied.
        SecurityNavigationCase{"DeniedDomain", "https://blocked.example/",
            NavigationPolicyState::Available, FilterMode::Denylist, true, true},
        // La política está disponible, pero la allowlist vacía no permite ningún dominio: cancela con DomainNotAllowed.
        SecurityNavigationCase{"EmptyAllowlist", "https://blocked.example/",
            NavigationPolicyState::Available, FilterMode::Allowlist, true, true},
        // La política está disponible y el filtrado activo rechaza una IP literal: cancela con IpBlocked.
        SecurityNavigationCase{"LiteralIp", "https://127.0.0.1/",
            NavigationPolicyState::Available, FilterMode::Denylist, true, true}),
    SecurityNavigationTest::CaseName);

}  // namespace sbp
