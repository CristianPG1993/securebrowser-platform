#include "security/url_filtering_config.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace sbp {

// Desactivar el filtrado conserva su modo y reglas para su posterior utilización.
TEST(UrlFilteringConfigTest, DisabledFilteringPreservesRulesAndMode) {
    const std::vector<std::string> rules{"example.com", "example.org"};
    const UrlFilteringConfig config(false, FilterMode::Allowlist, rules);

    EXPECT_FALSE(config.Enabled());
    EXPECT_EQ(FilterMode::Allowlist, config.Mode());
    EXPECT_EQ(rules, config.Rules());
}

// Una ALLOWLIST vacía debe conservarse sin sustituir su modo por DENYLIST.
TEST(UrlFilteringConfigTest, PreservesEmptyAllowlist) {
    const UrlFilteringConfig config(true, FilterMode::Allowlist, {});

    EXPECT_TRUE(config.Enabled());
    EXPECT_EQ(FilterMode::Allowlist, config.Mode());
    EXPECT_TRUE(config.Rules().empty());
}

// Modificar y vaciar el vector de origen no debe alterar la copia de la configuración.
TEST(UrlFilteringConfigTest, OwnsRulesIndependentlyOfInput) {
    std::vector<std::string> rules{"example.com", "example.org"};
    const std::vector<std::string> expectedRules = rules;
    const UrlFilteringConfig config(true, FilterMode::Denylist, rules);

    rules.front() = "changed.example";
    rules.clear();

    EXPECT_TRUE(config.Enabled());
    EXPECT_EQ(FilterMode::Denylist, config.Mode());
    EXPECT_EQ(expectedRules, config.Rules());
}

}  // namespace sbp
