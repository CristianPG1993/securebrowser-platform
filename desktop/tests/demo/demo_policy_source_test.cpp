#include "demo/demo_policy_source.h"
#include "security/policy_manager.h"
#include "security/url_filter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <initializer_list>
#include <optional>

namespace sbp {

class DemoPolicySourceTest : public ::testing::Test {
public:
    DemoPolicySourceTest();

protected:
    void SetUp() override;

    PolicyManager::Clock::time_point now_{};
    PolicyManager manager_;
    std::optional<SecurityContext> context_;
    UrlFilter filter_;
};

DemoPolicySourceTest::DemoPolicySourceTest()
    // El reloj solo se consulta en el hilo del test; now_ vive más que el gestor que lo captura.
    : manager_([this] { return now_; }, PolicyManager::Validity::Unlimited) {}

void DemoPolicySourceTest::SetUp() {
    const auto context = manager_.BeginContext(DemoPolicySource::DeviceId, DemoPolicySource::CompanyId);
    ASSERT_TRUE(context.has_value());
    context_.emplace(*context);
}

// La fuente libre debe superar la validación real y permitir los destinos públicos de la demostración.
TEST_F(DemoPolicySourceTest, FreePolicyPublishesAndAllowsPublicDomains) {
    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateFreePolicy()));

    for (const char* host : {
        "www.wikipedia.org", "facebook.com", "instagram.com", "tiktok.com", "x.com",
        "youtube.com", "www.youtube.com", "twitch.tv", "netflix.com"}) {
        SCOPED_TRACE(host);
        const UrlTarget target(true, "https", host, UrlHostKind::Domain);
        const auto decision = filter_.Evaluate(manager_.ReadPolicy(*context_), target);
        EXPECT_EQ(SecurityAction::Allow, decision.Action());
        EXPECT_EQ(SecurityReason::Allowed, decision.Reason());
    }
}

// La lista definitiva de E2 bloquea los siete dominios y sus subdominios mediante el motor existente.
TEST_F(DemoPolicySourceTest, RestrictedPolicyBlocksConfiguredDomainsAndSubdomains) {
    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateRestrictedPolicy()));

    for (const char* host : {
        "facebook.com", "www.facebook.com", "instagram.com", "www.instagram.com",
        "tiktok.com", "www.tiktok.com", "x.com", "www.x.com",
        "youtube.com", "www.youtube.com", "video.www.youtube.com",
        "twitch.tv", "www.twitch.tv", "netflix.com", "www.netflix.com"}) {
        SCOPED_TRACE(host);
        const UrlTarget target(true, "https", host, UrlHostKind::Domain);
        const auto decision = filter_.Evaluate(manager_.ReadPolicy(*context_), target);
        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(SecurityReason::DomainDenied, decision.Reason());
    }
}

// La DENYLIST no bloquea otros dominios ni nombres que solo contienen el texto de la regla.
TEST_F(DemoPolicySourceTest, RestrictedPolicyKeepsOtherDomainsAllowed) {
    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateRestrictedPolicy()));

    for (const char* host : {
        "www.wikipedia.org", "notyoutube.com", "youtube.com.example.org",
        "notfacebook.com", "facebook.com.example.org", "notx.com", "x.com.example.org"}) {
        SCOPED_TRACE(host);
        const UrlTarget target(true, "https", host, UrlHostKind::Domain);
        EXPECT_EQ(SecurityAction::Allow,
                  filter_.Evaluate(manager_.ReadPolicy(*context_), target).Action());
    }
}

// Publicar A, B y A en el mismo contexto cambia las decisiones nuevas sin crear otro motor.
TEST_F(DemoPolicySourceTest, SwitchingPoliciesChangesNewDecisions) {
    const UrlTarget target(true, "https", "www.youtube.com", UrlHostKind::Domain);

    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateFreePolicy()));
    EXPECT_EQ(SecurityReason::Allowed, filter_.Evaluate(manager_.ReadPolicy(*context_), target).Reason());

    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateRestrictedPolicy()));
    EXPECT_EQ(SecurityReason::DomainDenied, filter_.Evaluate(manager_.ReadPolicy(*context_), target).Reason());

    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateFreePolicy()));
    EXPECT_EQ(SecurityReason::Allowed, filter_.Evaluate(manager_.ReadPolicy(*context_), target).Reason());
}

// La demo mantiene ambas políticas tras el antiguo límite y al avanzar un día, sin republicarlas.
TEST_F(DemoPolicySourceTest, BothDemoPoliciesRemainAvailableWithoutExpiration) {
    const UrlTarget target(true, "https", "www.wikipedia.org", UrlHostKind::Domain);

    for (const bool restricted : {false, true}) {
        SCOPED_TRACE(restricted ? "restricted" : "free");
        const auto snapshot = restricted ? DemoPolicySource::CreateRestrictedPolicy()
                                         : DemoPolicySource::CreateFreePolicy();
        ASSERT_EQ(PolicyPublicationResult::Published, manager_.PublishPolicy(*context_, snapshot));

        const auto published_state = manager_.ReadState();
        now_ += std::chrono::minutes{15};
        EXPECT_EQ(PolicyAvailability::Available, manager_.ReadPolicy(*context_).availability);
        EXPECT_EQ(SecurityAction::Allow, filter_.Evaluate(manager_.ReadPolicy(*context_), target).Action());

        now_ += std::chrono::hours{24};
        EXPECT_EQ(PolicyAvailability::Available, manager_.ReadPolicy(*context_).availability);
        EXPECT_EQ(SecurityAction::Allow, filter_.Evaluate(manager_.ReadPolicy(*context_), target).Action());
        // El estado sigue siendo el publicado: no se ha renovado silenciosamente al leer o evaluar.
        EXPECT_EQ(published_state, manager_.ReadState());
        const UrlTarget social_target(true, "https", "www.youtube.com", UrlHostKind::Domain);
        EXPECT_EQ(restricted ? SecurityReason::DomainDenied : SecurityReason::Allowed,
                  filter_.Evaluate(manager_.ReadPolicy(*context_), social_target).Reason());
    }
}

// Sin caducidad no implica permiso sin política ni después de cerrar el contexto demo.
TEST_F(DemoPolicySourceTest, UnlimitedDemoStillBlocksWithoutPolicyOrAfterContextEnd) {
    const UrlTarget target(true, "https", "www.wikipedia.org", UrlHostKind::Domain);

    const auto missing_policy = filter_.Evaluate(manager_.ReadPolicy(*context_), target);
    EXPECT_EQ(SecurityAction::Block, missing_policy.Action());
    EXPECT_EQ(SecurityReason::PolicyUnavailable, missing_policy.Reason());

    ASSERT_EQ(PolicyPublicationResult::Published,
              manager_.PublishPolicy(*context_, DemoPolicySource::CreateFreePolicy()));
    ASSERT_TRUE(manager_.EndContext(*context_));
    const auto ended_context = filter_.Evaluate(manager_.ReadPolicy(*context_), target);
    EXPECT_EQ(SecurityAction::Block, ended_context.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, ended_context.Reason());
}

}  // namespace sbp
