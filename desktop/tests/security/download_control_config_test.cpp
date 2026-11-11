#include "security/download_control_config.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace sbp {

// Desactivar el control conserva su modo y reglas para su posterior utilización.
TEST(DownloadControlConfigTest, DisabledControlPreservesRulesAndMode) {
    const std::vector<std::string> rules{"pdf", "zip"};
    const DownloadControlConfig config(false, FilterMode::Allowlist, rules);

    EXPECT_FALSE(config.Enabled());
    EXPECT_EQ(FilterMode::Allowlist, config.Mode());
    EXPECT_EQ(rules, config.Rules());
}

// Una ALLOWLIST vacía debe conservarse sin sustituir su modo por DENYLIST.
TEST(DownloadControlConfigTest, PreservesEmptyAllowlist) {
    const DownloadControlConfig config(true, FilterMode::Allowlist, {});

    EXPECT_TRUE(config.Enabled());
    EXPECT_EQ(FilterMode::Allowlist, config.Mode());
    EXPECT_TRUE(config.Rules().empty());
}

// Modificar y vaciar el vector de origen no debe alterar las extensiones conservadas.
TEST(DownloadControlConfigTest, OwnsRulesIndependentlyOfInput) {
    std::vector<std::string> rules{"exe", "zip"};
    const std::vector<std::string> expectedRules = rules;
    const DownloadControlConfig config(true, FilterMode::Denylist, rules);

    rules.front() = "pdf";
    rules.clear();

    EXPECT_TRUE(config.Enabled());
    EXPECT_EQ(FilterMode::Denylist, config.Mode());
    EXPECT_EQ(expectedRules, config.Rules());
}

}  // namespace sbp
