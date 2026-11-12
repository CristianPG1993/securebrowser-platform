#include "security/url_target.h"

#include <gtest/gtest.h>

#include <string>

namespace sbp {

// Conserva los datos de un dominio interpretado sin decidir si está permitido.
TEST(UrlTargetTest, PreservesInterpretedDomain) {
    const UrlTarget target(true, "https", "example.com", UrlHostKind::Domain);

    EXPECT_TRUE(target.IsValid());
    EXPECT_EQ("https", target.Scheme());
    EXPECT_EQ("example.com", target.Host());
    EXPECT_EQ(UrlHostKind::Domain, target.HostKind());
}

// Un resultado de interpretación fallido permanece inválido y sin host clasificado.
TEST(UrlTargetTest, PreservesFailedInterpretation) {
    const UrlTarget target(false, "", "", UrlHostKind::Unknown);

    EXPECT_FALSE(target.IsValid());
    EXPECT_TRUE(target.Scheme().empty());
    EXPECT_TRUE(target.Host().empty());
    EXPECT_EQ(UrlHostKind::Unknown, target.HostKind());
}

// Cambiar las cadenas de origen no modifica los datos conservados por el objeto.
TEST(UrlTargetTest, OwnsStringsIndependentlyOfInput) {
    std::string scheme = "https";
    std::string host = "example.com";
    const UrlTarget target(true, scheme, host, UrlHostKind::Domain);

    scheme = "http";
    host.clear();

    EXPECT_TRUE(target.IsValid());
    EXPECT_EQ("https", target.Scheme());
    EXPECT_EQ("example.com", target.Host());
    EXPECT_EQ(UrlHostKind::Domain, target.HostKind());
}

}  // namespace sbp
