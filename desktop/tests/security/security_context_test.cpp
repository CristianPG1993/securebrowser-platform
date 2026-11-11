#include "security/security_context.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace sbp {

// Usa valores distintos que superan los 32 bits para detectar truncamientos o campos intercambiados.
TEST(SecurityContextTest, Preserves64BitContextIdentifiers) {
    constexpr std::uint64_t generation = 4'294'967'297;
    constexpr std::int64_t deviceId = 5'000'000'001;
    constexpr std::int64_t companyId = 6'000'000'002;
    const SecurityContext context(generation, deviceId, companyId);

    EXPECT_EQ(generation, context.Generation());
    EXPECT_EQ(deviceId, context.DeviceId());
    EXPECT_EQ(companyId, context.CompanyId());
}

// Describe el contexto que se compara con la referencia y si debe considerarse igual.
struct SecurityContextEqualityCase {
    const char* label;
    std::uint64_t generation;
    std::int64_t deviceId;
    std::int64_t companyId;
    bool expectedEqual;
};

class SecurityContextEqualityTest
    : public ::testing::TestWithParam<SecurityContextEqualityCase> {};

// Cambia un identificador cada vez para comprobar que los tres forman parte de la identidad.
TEST_P(SecurityContextEqualityTest, ComparesCompleteContextIdentity) {
    const SecurityContextEqualityCase& testCase = GetParam();
    const SecurityContext current(1, 10, 20);
    const SecurityContext other(testCase.generation, testCase.deviceId, testCase.companyId);

    EXPECT_EQ(testCase.expectedEqual, current == other);
    EXPECT_EQ(testCase.expectedEqual, other == current);
}

INSTANTIATE_TEST_SUITE_P(
    ContextIdentity,
    SecurityContextEqualityTest,
    ::testing::Values(
        SecurityContextEqualityCase{"SameContext", 1, 10, 20, true},
        SecurityContextEqualityCase{"DifferentGeneration", 2, 10, 20, false},
        SecurityContextEqualityCase{"DifferentDevice", 1, 11, 20, false},
        SecurityContextEqualityCase{"DifferentCompany", 1, 10, 21, false}),
    // Nombra cada ejecución según el identificador que cambia respecto al contexto de referencia.
    [](const ::testing::TestParamInfo<SecurityContextEqualityCase>& info) {
        return std::string(info.param.label);
    });

}  // namespace sbp
