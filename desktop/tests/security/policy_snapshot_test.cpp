#include "security/policy_snapshot.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace sbp {

// Conserva identificadores de 64 bits y fechas distintas con precisión de microsegundos.
TEST(PolicySnapshotTest, PreservesIdentityNameAndBackendTimestamps) {
    constexpr std::int64_t policyId = 5'000'000'001;
    constexpr std::int64_t companyId = 6'000'000'002;
    const std::string name = "Office policy";
    const PolicySnapshot::Timestamp createdAt{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot::Timestamp updatedAt{std::chrono::microseconds{1'700'000'001'654'321}};
    const PolicySnapshot snapshot(
        policyId, companyId, name,
        UrlFilteringConfig(true, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Allowlist, {}),
        createdAt, updatedAt);

    EXPECT_EQ(policyId, snapshot.Id());
    EXPECT_EQ(companyId, snapshot.CompanyId());
    EXPECT_EQ(name, snapshot.Name());
    EXPECT_EQ(createdAt, snapshot.CreatedAt());
    EXPECT_EQ(updatedAt, snapshot.UpdatedAt());
}

// Las protecciones conservan sus propios indicadores, modos y reglas al agruparse.
TEST(PolicySnapshotTest, PreservesDistinctProtectionConfigurations) {
    const std::vector<std::string> domains{"example.com", "example.org"};
    const std::vector<std::string> extensions{"exe", "zip"};
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot snapshot(
        1, 2, "Office policy",
        UrlFilteringConfig(false, FilterMode::Allowlist, domains),
        DownloadControlConfig(true, FilterMode::Denylist, extensions),
        timestamp, timestamp);

    EXPECT_FALSE(snapshot.UrlFiltering().Enabled());
    EXPECT_EQ(FilterMode::Allowlist, snapshot.UrlFiltering().Mode());
    EXPECT_EQ(domains, snapshot.UrlFiltering().Rules());
    EXPECT_TRUE(snapshot.DownloadControl().Enabled());
    EXPECT_EQ(FilterMode::Denylist, snapshot.DownloadControl().Mode());
    EXPECT_EQ(extensions, snapshot.DownloadControl().Rules());
}

// Cambiar los objetos de entrada después de construir el snapshot no altera sus datos.
TEST(PolicySnapshotTest, OwnsNameAndProtectionConfigurationsIndependentlyOfInput) {
    std::string name = "Office policy";
    const std::vector<std::string> domains{"example.com"};
    const std::vector<std::string> extensions{"exe"};
    UrlFilteringConfig urlFiltering(true, FilterMode::Denylist, domains);
    DownloadControlConfig downloadControl(false, FilterMode::Allowlist, extensions);
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot snapshot(
        1, 2, name, urlFiltering, downloadControl, timestamp, timestamp);

    name = "Changed policy";
    urlFiltering = UrlFilteringConfig(false, FilterMode::Allowlist, {});
    downloadControl = DownloadControlConfig(true, FilterMode::Denylist, {});

    EXPECT_EQ("Office policy", snapshot.Name());
    EXPECT_TRUE(snapshot.UrlFiltering().Enabled());
    EXPECT_EQ(FilterMode::Denylist, snapshot.UrlFiltering().Mode());
    EXPECT_EQ(domains, snapshot.UrlFiltering().Rules());
    EXPECT_FALSE(snapshot.DownloadControl().Enabled());
    EXPECT_EQ(FilterMode::Allowlist, snapshot.DownloadControl().Mode());
    EXPECT_EQ(extensions, snapshot.DownloadControl().Rules());
}

}  // namespace sbp
