#include "security/url_filter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sbp {

// Un destino válido no permite navegar cuando no existe un contexto de seguridad.
TEST(UrlFilterTest, BlocksWithoutSecurityContext) {
    const UrlFilter filter;
    const PolicyManager::PolicyReadResult policy{};
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Un contexto válido sigue bloqueado hasta disponer de una política completa.
TEST(UrlFilterTest, BlocksCurrentContextWithoutPolicy) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const auto policy = manager.ReadPolicy(*context);
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
}

// Una política vigente con el filtrado desactivado permite un destino web válido.
TEST(UrlFilterTest, AllowsValidTargetWithFreshPolicyAndFilteringDisabled) {
    // El reloj fijo mantiene la política vigente sin depender del tiempo real.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Allowlist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));

    const auto policy = manager.ReadPolicy(*context);
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Allow, decision.Action());
    EXPECT_EQ(SecurityReason::Allowed, decision.Reason());
}

// Desactivar el filtrado no permite utilizar una política que ha alcanzado su caducidad.
TEST(UrlFilterTest, BlocksExpiredPolicyEvenWhenFilteringDisabled) {
    PolicyManager::Clock::time_point now{};
    // La referencia vive hasta después del gestor y se utiliza solo en el hilo del test.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Allowlist, {}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));

    now += std::chrono::minutes{15};
    const auto policy = manager.ReadPolicy(*context);
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::PolicyExpired, decision.Reason());
}

// Una consulta posterior al cierre ya no acredita el contexto de la operación.
TEST(UrlFilterTest, BlocksEndedContext) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_TRUE(manager.EndContext(*context));

    const auto policy = manager.ReadPolicy(*context);
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Volver al mismo Device y compañía no hace vigente el contexto de la sesión anterior.
TEST(UrlFilterTest, BlocksContextReplacedByNewGeneration) {
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    ASSERT_NE(previous->Generation(), current->Generation());

    const auto policy = manager.ReadPolicy(*previous);
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Datos ya interpretados: estas pruebas no acreditan el parsing real de CEF.
struct UrlFilterTargetCase {
    const char* name;
    bool valid;
    const char* scheme;
    const char* host;
    UrlHostKind hostKind;
    SecurityReason expectedReason;
};

// Asocia cada combinación de datos del destino con la misma comprobación de bloqueo.
class UrlFilterTargetTest : public ::testing::TestWithParam<UrlFilterTargetCase> {};

// Los controles generales del destino se mantienen aunque se desactive el filtrado.
TEST_P(UrlFilterTargetTest, BlocksInvalidOrUnsupportedTarget) {
    const UrlFilterTargetCase& testCase = GetParam();

    for (const bool filteringEnabled : {false, true}) {
        SCOPED_TRACE(filteringEnabled ? "filtering enabled" : "filtering disabled");
        // El reloj fijo mantiene vigente la política durante ambas comprobaciones.
        PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
        const auto context = manager.BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());

        const PolicySnapshot snapshot(
            1, 20, "Office policy",
            UrlFilteringConfig(filteringEnabled, FilterMode::Denylist, {}),
            DownloadControlConfig(false, FilterMode::Denylist, {}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));

        const auto policy = manager.ReadPolicy(*context);
        const UrlFilter filter;
        const UrlTarget target(testCase.valid, testCase.scheme, testCase.host, testCase.hostKind);

        const SecurityDecision decision = filter.Evaluate(policy, target);

        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(testCase.expectedReason, decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(
    InterpretedTargets,
    UrlFilterTargetTest,
    ::testing::Values(
        UrlFilterTargetCase{"FailedInterpretation", false, "https", "example.com",
            UrlHostKind::Domain, SecurityReason::InvalidUrl},
        UrlFilterTargetCase{"EmptyHost", true, "https", "",
            UrlHostKind::Domain, SecurityReason::InvalidUrl},
        UrlFilterTargetCase{"UnknownHostKind", true, "https", "example.com",
            UrlHostKind::Unknown, SecurityReason::InvalidUrl},
        UrlFilterTargetCase{"UnexpectedHostKind", true, "https", "example.com",
            static_cast<UrlHostKind>(-1), SecurityReason::InvalidUrl},
        UrlFilterTargetCase{"FileScheme", true, "file", "",
            UrlHostKind::Unknown, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"DataScheme", true, "data", "",
            UrlHostKind::Unknown, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"BlobScheme", true, "blob", "",
            UrlHostKind::Unknown, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"FtpScheme", true, "ftp", "example.com",
            UrlHostKind::Domain, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"MailtoScheme", true, "mailto", "",
            UrlHostKind::Unknown, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"TelScheme", true, "tel", "",
            UrlHostKind::Unknown, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"UnknownScheme", true, "custom", "example.com",
            UrlHostKind::Domain, SecurityReason::UnsupportedScheme},
        UrlFilterTargetCase{"EmptyScheme", true, "", "example.com",
            UrlHostKind::Domain, SecurityReason::UnsupportedScheme}),
    // Nombra cada caso con sus datos de registro; no captura referencias externas.
    [](const ::testing::TestParamInfo<UrlFilterTargetCase>& info) {
        return std::string(info.param.name);
    });

// La tabla fija la coincidencia esperada sobre hosts ASCII ya preparados por el adaptador.
struct UrlFilterDomainCase {
    std::string name;
    std::vector<std::string> rules;
    std::string host;
    bool matches;
};

class UrlFilterDomainTest : public ::testing::TestWithParam<UrlFilterDomainCase> {};

// Cada dominio se evalúa en ambos esquemas, modos y estados de activación.
TEST_P(UrlFilterDomainTest, AppliesDomainRulesAndActivation) {
    const UrlFilterDomainCase& testCase = GetParam();
    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // El reloj fijo hace independiente esta matriz del tiempo de ejecución.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(enabled, mode, testCase.rules),
                DownloadControlConfig(false, FilterMode::Denylist, {}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            const UrlFilter filter;

            // La tabla indica la coincidencia; el contrato del modo determina la decisión esperada.
            const bool allowed = !enabled
                || (mode == FilterMode::Denylist ? !testCase.matches : testCase.matches);
            const SecurityReason expectedReason = allowed ? SecurityReason::Allowed
                : (mode == FilterMode::Denylist
                    ? SecurityReason::DomainDenied : SecurityReason::DomainNotAllowed);

            for (const char* scheme : {"http", "https"}) {
                SCOPED_TRACE(scheme);
                const UrlTarget target(true, scheme, testCase.host, UrlHostKind::Domain);
                const SecurityDecision decision = filter.Evaluate(policy, target);
                EXPECT_EQ(allowed ? SecurityAction::Allow : SecurityAction::Block, decision.Action());
                EXPECT_EQ(expectedReason, decision.Reason());
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    DomainRules,
    UrlFilterDomainTest,
    ::testing::Values(
        UrlFilterDomainCase{"Exact", {"example.com"}, "example.com", true},
        UrlFilterDomainCase{"Www", {"example.com"}, "www.example.com", true},
        UrlFilterDomainCase{"Shop", {"example.com"}, "shop.example.com", true},
        UrlFilterDomainCase{"DeepSubdomain", {"example.com"}, "a.b.example.com", true},
        UrlFilterDomainCase{"LeadingCollision", {"example.com"}, "myexample.com", false},
        UrlFilterDomainCase{"HyphenCollision", {"example.com"}, "not-example.com", false},
        UrlFilterDomainCase{"TrailingCollision", {"example.com"}, "example.com.evil.com", false},
        UrlFilterDomainCase{"DifferentTld", {"example.com"}, "example.company", false},
        UrlFilterDomainCase{"ShorterHost", {"example.com"}, "com", false},
        UrlFilterDomainCase{"PrefixOnly", {"example.com"}, "example", false},
        UrlFilterDomainCase{"Unrelated", {"example.com"}, "other.example", false},
        UrlFilterDomainCase{"MultipleNoMatch", {"one.example", "two.example"}, "example.com", false},
        UrlFilterDomainCase{"FirstRuleMatches", {"example.com", "one.example", "two.example"},
            "www.example.com", true},
        UrlFilterDomainCase{"MiddleRuleMatches", {"one.example", "example.com", "two.example"},
            "www.example.com", true},
        UrlFilterDomainCase{"LastRuleMatches", {"one.example", "two.example", "example.com"},
            "www.example.com", true},
        UrlFilterDomainCase{"SpecificSubdomain", {"shop.example.com"}, "shop.example.com", true},
        UrlFilterDomainCase{"SpecificDescendant", {"shop.example.com"}, "a.shop.example.com", true},
        UrlFilterDomainCase{"ParentNotCovered", {"shop.example.com"}, "example.com", false},
        UrlFilterDomainCase{"SiblingNotCovered", {"shop.example.com"}, "www.example.com", false},
        UrlFilterDomainCase{"EmptyRules", {}, "example.com", false},
        UrlFilterDomainCase{"UppercaseRule", {"EXAMPLE.COM"}, "example.com", true},
        UrlFilterDomainCase{"FinalDotRule", {"example.com."}, "www.example.com", true},
        UrlFilterDomainCase{"UppercaseFinalDotRule", {"EXAMPLE.COM."}, "example.com", true},
        UrlFilterDomainCase{"IdnAsciiExact", {"xn--bcher-kva.de"}, "xn--bcher-kva.de", true},
        UrlFilterDomainCase{"IdnAsciiSubdomain", {"xn--bcher-kva.de"}, "shop.xn--bcher-kva.de", true},
        UrlFilterDomainCase{"NoIdnAlias", {"fass.de"}, "xn--fa-hia.de", false},
        UrlFilterDomainCase{"DifferentPunycode", {"example.com"}, "xn--bcher-kva.com", false},
        UrlFilterDomainCase{"NumericLabelIsDomain", {"example.com"}, "123.example.com", true},
        UrlFilterDomainCase{"SingleCharacterRule", {"a"}, "b.a", true},
        UrlFilterDomainCase{"SingleCharacterCollision", {"a"}, "ba", false},
        UrlFilterDomainCase{"MaximumLabel", {"example.com"}, std::string(63, 'a') + ".example.com", true},
        UrlFilterDomainCase{"MaximumDomainWithRuleFinalDot",
            {std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(61, 'd') + "."},
            std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(61, 'd'), true}),
    // Nombres estables de los casos; no se capturan datos externos.
    [](const ::testing::TestParamInfo<UrlFilterDomainCase>& info) { return info.param.name; });

// La clasificación IP se recibe del adaptador; las variantes de URL se probarán con CEF.
struct UrlFilterIpCase {
    std::string name;
    std::string host;
    UrlHostKind hostKind;
    std::vector<std::string> rules;
};

class UrlFilterIpTest : public ::testing::TestWithParam<UrlFilterIpCase> {};

// Las IP se bloquean en ambos modos activos y omiten las reglas cuando el filtrado está desactivado.
TEST_P(UrlFilterIpTest, AppliesIpRestrictionWithoutDnsSuffixMatching) {
    const UrlFilterIpCase& testCase = GetParam();
    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // Todas las evaluaciones parten de una política recién validada con reloj fijo.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(enabled, mode, testCase.rules),
                DownloadControlConfig(false, FilterMode::Denylist, {}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            const UrlFilter filter;

            for (const char* scheme : {"http", "https"}) {
                SCOPED_TRACE(scheme);
                const UrlTarget target(true, scheme, testCase.host, testCase.hostKind);
                const SecurityDecision decision = filter.Evaluate(policy, target);
                EXPECT_EQ(enabled ? SecurityAction::Block : SecurityAction::Allow, decision.Action());
                EXPECT_EQ(enabled ? SecurityReason::IpBlocked : SecurityReason::Allowed, decision.Reason());
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    ClassifiedAddresses,
    UrlFilterIpTest,
    ::testing::Values(
        UrlFilterIpCase{"Ipv4EmptyRules", "192.0.2.1", UrlHostKind::Ipv4, {}},
        UrlFilterIpCase{"Ipv4ExactRule", "192.0.2.1", UrlHostKind::Ipv4, {"192.0.2.1"}},
        UrlFilterIpCase{"Ipv4SuffixRule", "192.0.2.1", UrlHostKind::Ipv4, {"0.2.1"}},
        UrlFilterIpCase{"Ipv4Loopback", "127.0.0.1", UrlHostKind::Ipv4, {}},
        UrlFilterIpCase{"Ipv6EmptyRules", "2001:db8::1", UrlHostKind::Ipv6, {}},
        UrlFilterIpCase{"Ipv6Loopback", "::1", UrlHostKind::Ipv6, {}},
        UrlFilterIpCase{"Ipv6WithDomainRule", "2001:db8::1", UrlHostKind::Ipv6, {"example.com"}}),
    // Cada caso identifica una dirección ya clasificada, sin capturas externas.
    [](const ::testing::TestParamInfo<UrlFilterIpCase>& info) { return info.param.name; });

// Combina problemas para comprobar qué motivo debe prevalecer, sin depender de la GUI.
struct UrlFilterPriorityCase {
    const char* name;
    PolicyAvailability availability;
    bool valid;
    const char* scheme;
    const char* host;
    UrlHostKind hostKind;
    SecurityReason expectedReason;
};

class UrlFilterPriorityTest : public ::testing::TestWithParam<UrlFilterPriorityCase> {};

TEST_P(UrlFilterPriorityTest, PreservesFirstBlockingReason) {
    const UrlFilterPriorityCase& testCase = GetParam();
    PolicyManager::Clock::time_point now{};
    // now vive durante toda la prueba y solo se consulta en este hilo.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    if (testCase.availability != PolicyAvailability::Unavailable) {
        const PolicySnapshot snapshot(
            1, 20, "Office policy",
            UrlFilteringConfig(true, FilterMode::Denylist, {"example.com", "192.0.2.1"}),
            DownloadControlConfig(false, FilterMode::Denylist, {}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    }
    if (testCase.availability == PolicyAvailability::Expired) {
        now += std::chrono::minutes{15};
    }

    // Consultar otra generación produce un contexto inválido sin inventar una lectura permisiva.
    const SecurityContext requested(
        testCase.availability == PolicyAvailability::InvalidContext
            ? context->Generation() + 1 : context->Generation(),
        context->DeviceId(), context->CompanyId());
    const auto policy = manager.ReadPolicy(requested);
    ASSERT_EQ(testCase.availability, policy.availability);
    const UrlFilter filter;
    const UrlTarget target(testCase.valid, testCase.scheme, testCase.host, testCase.hostKind);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(testCase.expectedReason, decision.Reason());
}

INSTANTIATE_TEST_SUITE_P(
    BlockingOrder,
    UrlFilterPriorityTest,
    ::testing::Values(
        UrlFilterPriorityCase{"ContextBeforeInvalidUrl", PolicyAvailability::InvalidContext,
            false, "https", "example.com", UrlHostKind::Domain, SecurityReason::InvalidContext},
        UrlFilterPriorityCase{"ContextBeforeUnsupportedScheme", PolicyAvailability::InvalidContext,
            true, "file", "", UrlHostKind::Unknown, SecurityReason::InvalidContext},
        UrlFilterPriorityCase{"UnavailableBeforeInvalidUrl", PolicyAvailability::Unavailable,
            false, "https", "example.com", UrlHostKind::Domain, SecurityReason::PolicyUnavailable},
        UrlFilterPriorityCase{"UnavailableBeforeUnsupportedScheme", PolicyAvailability::Unavailable,
            true, "file", "", UrlHostKind::Unknown, SecurityReason::PolicyUnavailable},
        UrlFilterPriorityCase{"ExpiredBeforeInvalidUrl", PolicyAvailability::Expired,
            false, "https", "example.com", UrlHostKind::Domain, SecurityReason::PolicyExpired},
        UrlFilterPriorityCase{"ExpiredBeforeUnsupportedScheme", PolicyAvailability::Expired,
            true, "file", "", UrlHostKind::Unknown, SecurityReason::PolicyExpired},
        UrlFilterPriorityCase{"InvalidUrlBeforeUnsupportedScheme", PolicyAvailability::Available,
            false, "file", "", UrlHostKind::Unknown, SecurityReason::InvalidUrl},
        UrlFilterPriorityCase{"InvalidUrlBeforeIp", PolicyAvailability::Available,
            false, "https", "192.0.2.1", UrlHostKind::Ipv4, SecurityReason::InvalidUrl},
        UrlFilterPriorityCase{"UnsupportedSchemeBeforeIp", PolicyAvailability::Available,
            true, "ftp", "192.0.2.1", UrlHostKind::Ipv4, SecurityReason::UnsupportedScheme},
        UrlFilterPriorityCase{"UnclassifiedHostBeforeDomainRules", PolicyAvailability::Available,
            true, "https", "example.com", UrlHostKind::Unknown, SecurityReason::InvalidUrl},
        UrlFilterPriorityCase{"IpBeforeMatchingDenylist", PolicyAvailability::Available,
            true, "https", "192.0.2.1", UrlHostKind::Ipv4, SecurityReason::IpBlocked}),
    // El nombre expresa la prioridad que debe mantenerse; no se capturan referencias.
    [](const ::testing::TestParamInfo<UrlFilterPriorityCase>& info) {
        return std::string(info.param.name);
    });

// Entradas incoherentes construidas únicamente para comprobar el rechazo defensivo del lector.
struct UrlFilterReadCase {
    const char* name;
    PolicyAvailability availability;
    bool hasState;
    bool hasPolicy;
    bool hasTimestamp;
    SecurityReason expectedReason;
};

class UrlFilterReadTest : public ::testing::TestWithParam<UrlFilterReadCase> {};

TEST_P(UrlFilterReadTest, RejectsIncompleteOrUnusableRead) {
    const UrlFilterReadCase& testCase = GetParam();
    const auto snapshot = std::make_shared<const PolicySnapshot>(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Allowlist, std::vector<std::string>{}),
        DownloadControlConfig(false, FilterMode::Denylist, std::vector<std::string>{}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    const auto state = std::make_shared<const PolicyManager::State>(PolicyManager::State{
        SecurityContext(1, 10, 20),
        testCase.hasPolicy ? snapshot : nullptr,
        testCase.hasTimestamp
            ? std::optional<PolicyManager::Clock::time_point>{PolicyManager::Clock::time_point{}}
            : std::nullopt});
    const PolicyManager::PolicyReadResult policy{
        testCase.availability, testCase.hasState ? state : nullptr};
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    const SecurityDecision decision = filter.Evaluate(policy, target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(testCase.expectedReason, decision.Reason());
}

INSTANTIATE_TEST_SUITE_P(
    DefensiveReads,
    UrlFilterReadTest,
    ::testing::Values(
        UrlFilterReadCase{"AvailableWithoutState", PolicyAvailability::Available,
            false, true, true, SecurityReason::InvalidContext},
        UrlFilterReadCase{"UnavailableWithoutState", PolicyAvailability::Unavailable,
            false, false, false, SecurityReason::InvalidContext},
        UrlFilterReadCase{"AvailableWithoutPolicy", PolicyAvailability::Available,
            true, false, true, SecurityReason::PolicyUnavailable},
        UrlFilterReadCase{"AvailableWithoutTimestamp", PolicyAvailability::Available,
            true, true, false, SecurityReason::PolicyUnavailable},
        UrlFilterReadCase{"UnknownAvailability", static_cast<PolicyAvailability>(-1),
            true, true, true, SecurityReason::PolicyUnavailable},
        UrlFilterReadCase{"InvalidContextWithCompleteState", PolicyAvailability::InvalidContext,
            true, true, true, SecurityReason::InvalidContext},
        UrlFilterReadCase{"UnavailableWithCompleteState", PolicyAvailability::Unavailable,
            true, true, true, SecurityReason::PolicyUnavailable}),
    // Los nombres distinguen entradas incoherentes; la lambda no conserva referencias externas.
    [](const ::testing::TestParamInfo<UrlFilterReadCase>& info) { return std::string(info.param.name); });

// Se comprueba también el límite exacto de vigencia que recibe el evaluador desde el gestor.
struct UrlFilterExpiryCase {
    const char* name;
    PolicyManager::Clock::duration elapsed;
    SecurityReason expectedReason;
};

class UrlFilterExpiryTest : public ::testing::TestWithParam<UrlFilterExpiryCase> {};

TEST_P(UrlFilterExpiryTest, UsesAvailabilityAtExpiryBoundary) {
    const UrlFilterExpiryCase& testCase = GetParam();
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        PolicyManager::Clock::time_point now{};
        // Referencia local utilizada de forma secuencial y viva hasta destruir el gestor.
        PolicyManager manager([&now] { return now; });
        const auto context = manager.BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());
        const PolicySnapshot snapshot(
            1, 20, "Office policy",
            UrlFilteringConfig(enabled, FilterMode::Denylist, {}),
            DownloadControlConfig(false, FilterMode::Denylist, {}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
        now += testCase.elapsed;
        const auto policy = manager.ReadPolicy(*context);
        const UrlFilter filter;
        const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

        const SecurityDecision decision = filter.Evaluate(policy, target);

        EXPECT_EQ(testCase.expectedReason, decision.Reason());
        EXPECT_EQ(testCase.expectedReason == SecurityReason::Allowed
            ? SecurityAction::Allow : SecurityAction::Block, decision.Action());
    }
}

INSTANTIATE_TEST_SUITE_P(
    LocalExpiry,
    UrlFilterExpiryTest,
    ::testing::Values(
        UrlFilterExpiryCase{"JustBeforeDeadline",
            std::chrono::minutes{15} - PolicyManager::Clock::duration{1}, SecurityReason::Allowed},
        UrlFilterExpiryCase{"AtDeadline", std::chrono::minutes{15}, SecurityReason::PolicyExpired},
        UrlFilterExpiryCase{"AfterDeadline",
            std::chrono::minutes{15} + PolicyManager::Clock::duration{1}, SecurityReason::PolicyExpired}),
    // Identifica los límites temporales sin usar esperas ni capturar objetos del test.
    [](const ::testing::TestParamInfo<UrlFilterExpiryCase>& info) { return std::string(info.param.name); });

// Un snapshot rechazado inicialmente no habilita operaciones ni siquiera con filtrado desactivado.
TEST(UrlFilterTest, RejectedInitialPolicyRemainsUnavailable) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot invalid(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {""}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*context, invalid));

    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);
    const auto decision = filter.Evaluate(manager.ReadPolicy(*context), target);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
}

// Un fallo de actualización no cambia las reglas anteriores ni prolonga su vigencia.
TEST(UrlFilterTest, RejectedReplacementPreservesDecisionAndExpiry) {
    PolicyManager::Clock::time_point now{};
    // El reloj controlado y sus referencias se usan solo durante este test secuencial.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot original(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"example.com"}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, original));
    now += std::chrono::minutes{14};
    const PolicySnapshot invalid(
        2, 20, "Invalid replacement",
        UrlFilteringConfig(false, FilterMode::Allowlist, {""}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*context, invalid));
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);
    const auto beforeExpiry = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Block, beforeExpiry.Action());
    EXPECT_EQ(SecurityReason::DomainDenied, beforeExpiry.Reason());

    now += std::chrono::minutes{1};
    const auto expired = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Block, expired.Action());
    EXPECT_EQ(SecurityReason::PolicyExpired, expired.Reason());
}

// Una nueva lectura refleja las reglas publicadas posteriormente para el mismo contexto.
TEST(UrlFilterTest, FreshReadUsesReplacementRules) {
    // Mantiene ambas publicaciones vigentes para aislar el cambio de reglas.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot denied(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"example.com"}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, denied));
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);
    const auto before = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Block, before.Action());
    EXPECT_EQ(SecurityReason::DomainDenied, before.Reason());

    const PolicySnapshot allowed(
        2, 20, "Updated policy",
        UrlFilteringConfig(true, FilterMode::Allowlist, {"example.com"}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, allowed));
    const auto after = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Allow, after.Action());
    EXPECT_EQ(SecurityReason::Allowed, after.Reason());
}

// Evaluar repetidamente no consulta el reloj ni modifica las reglas o el instante validado.
TEST(UrlFilterTest, RepeatedEvaluationPreservesStateAndDoesNotReadClock) {
    int clockReads = 0;
    // El contador se captura por referencia y vive más que el gestor; todas las llamadas son secuenciales.
    PolicyManager manager([&clockReads] {
        ++clockReads;
        return PolicyManager::Clock::time_point{};
    });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const std::vector<std::string> originalRules{"EXAMPLE.COM."};
    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, originalRules),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    const auto policy = manager.ReadPolicy(*context);
    ASSERT_TRUE(policy.state);
    const int readsBeforeEvaluation = clockReads;
    const auto validatedAt = policy.state->validatedAt;
    const UrlFilter filter;
    const UrlTarget target(true, "https", "www.example.com", UrlHostKind::Domain);

    for (int index = 0; index < 5; ++index) {
        const auto decision = filter.Evaluate(policy, target);
        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(SecurityReason::DomainDenied, decision.Reason());
    }

    EXPECT_EQ(readsBeforeEvaluation, clockReads);
    EXPECT_EQ(policy.state, manager.ReadState());
    EXPECT_EQ(validatedAt, policy.state->validatedAt);
    EXPECT_EQ(originalRules, policy.state->policy->UrlFiltering().Rules());
}

// Una evaluación correcta a los catorce minutos no renueva el plazo local de la política.
TEST(UrlFilterTest, EvaluationDoesNotRenewPolicy) {
    PolicyManager::Clock::time_point now{};
    // El tiempo se controla en el hilo del test y permanece vivo durante el uso del gestor.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Allowlist, {"example.com"}),
        DownloadControlConfig(false, FilterMode::Denylist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    const UrlFilter filter;
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);
    now += std::chrono::minutes{14};
    const auto fresh = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Allow, fresh.Action());
    EXPECT_EQ(SecurityReason::Allowed, fresh.Reason());

    now += std::chrono::minutes{1};
    const auto expired = filter.Evaluate(manager.ReadPolicy(*context), target);
    EXPECT_EQ(SecurityAction::Block, expired.Action());
    EXPECT_EQ(SecurityReason::PolicyExpired, expired.Reason());
}

// Esta lectura incoherente solo se construye en el test: PolicyManager rechaza esos modos al publicar.
TEST(UrlFilterTest, UnknownModeNeverAllowsNavigation) {
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        const auto snapshot = std::make_shared<const PolicySnapshot>(
            1, 20, "Invalid policy",
            UrlFilteringConfig(enabled, static_cast<FilterMode>(-1), std::vector<std::string>{}),
            DownloadControlConfig(false, FilterMode::Denylist, std::vector<std::string>{}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        const auto state = std::make_shared<const PolicyManager::State>(PolicyManager::State{
            SecurityContext(1, 10, 20), snapshot, PolicyManager::Clock::time_point{}});
        const PolicyManager::PolicyReadResult policy{PolicyAvailability::Available, state};
        const UrlFilter filter;
        const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

        const auto decision = filter.Evaluate(policy, target);

        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
    }
}

}  // namespace sbp
