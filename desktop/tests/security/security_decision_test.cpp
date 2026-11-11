#include "security/security_decision.h"

#include <gtest/gtest.h>

namespace sbp {

// Comprueba que el motivo permitido autoriza la operación y se conserva.
TEST(SecurityDecisionTest, AllowedReasonAllowsOperationAndIsPreserved) {
    const SecurityDecision decision(SecurityReason::Allowed);

    EXPECT_EQ(SecurityAction::Allow, decision.Action());
    EXPECT_EQ(SecurityReason::Allowed, decision.Reason());
}

// Permite ejecutar la misma comprobación con cada motivo de bloqueo.
class BlockingSecurityDecisionTest : public ::testing::TestWithParam<SecurityReason> {};

// Comprueba la acción y la conservación del motivo recibido por el constructor.
TEST_P(BlockingSecurityDecisionTest, BlocksOperationAndPreservesReason) {
    const SecurityReason reason = GetParam();
    const SecurityDecision decision(reason);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(reason, decision.Reason());
}

// Cubre todos los motivos de bloqueo definidos para el contrato actual.
INSTANTIATE_TEST_SUITE_P(
    AllBlockingReasons,
    BlockingSecurityDecisionTest,
    ::testing::Values(
        SecurityReason::InvalidContext,
        SecurityReason::InstallationUnauthorized,
        SecurityReason::PolicyUnavailable,
        SecurityReason::PolicyExpired,
        SecurityReason::InvalidUrl,
        SecurityReason::UnsupportedScheme,
        SecurityReason::IpBlocked,
        SecurityReason::DomainDenied,
        SecurityReason::DomainNotAllowed,
        SecurityReason::InvalidFileName,
        SecurityReason::MissingExtension,
        SecurityReason::ExtensionDenied,
        SecurityReason::ExtensionNotAllowed));

}  // namespace sbp
