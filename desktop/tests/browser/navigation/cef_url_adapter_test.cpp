#include "browser/cef_url_adapter.h"

#include <gtest/gtest.h>

#include <string>

namespace sbp {

// Las entradas se interpretan mediante el parser real del SDK CEF fijado.
struct CefUrlAdapterCase {
    const char* name;
    const char* url;
    bool valid;
    const char* scheme;
    const char* host;
    UrlHostKind hostKind;
};

class CefUrlAdapterTest : public ::testing::TestWithParam<CefUrlAdapterCase> {};

// Comprueba los datos transferidos al núcleo, sin afirmar que se haya cancelado una navegación.
TEST_P(CefUrlAdapterTest, ProducesExpectedTarget) {
    const CefUrlAdapterCase& testCase = GetParam();
    const CefUrlAdapter adapter;

    const UrlTarget target = adapter.Parse(CefString(testCase.url));

    EXPECT_EQ(testCase.valid, target.IsValid());
    EXPECT_EQ(testCase.scheme, target.Scheme());
    EXPECT_EQ(testCase.host, target.Host());
    EXPECT_EQ(testCase.hostKind, target.HostKind());
}

INSTANTIATE_TEST_SUITE_P(
    CefParser,
    CefUrlAdapterTest,
    ::testing::Values(
        CefUrlAdapterCase{"HttpsWithPathQueryFragment", "https://example.com/path?q=value#section",
            true, "https", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"HttpWithPort", "http://example.com:8080/path",
            true, "http", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"UppercaseSchemeAndHost", "HTTPS://EXAMPLE.COM/path",
            true, "https", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"FinalDot", "https://EXAMPLE.COM./path",
            true, "https", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"UserInfo", "https://user:password@example.com:8443/path",
            true, "https", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"UserInfoIsNotHost", "https://example.com@evil.example/path",
            true, "https", "evil.example", UrlHostKind::Domain},
        CefUrlAdapterCase{"EncodedHost", "https://%65xample.com/path",
            true, "https", "example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"NumericSubdomain", "https://123.example.com/path",
            true, "https", "123.example.com", UrlHostKind::Domain},
        CefUrlAdapterCase{"Ipv4", "http://127.0.0.1:8080/path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"ShortIpv4", "http://127.1/path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"DecimalIpv4", "http://2130706433/path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"HexadecimalIpv4", "http://0x7f000001/path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"OctalIpv4", "http://0177.0.0.1/path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"Ipv4WithFinalDot", "http://127.0.0.1./path",
            true, "http", "127.0.0.1", UrlHostKind::Ipv4},
        CefUrlAdapterCase{"Ipv6Loopback", "http://[::1]:8080/path",
            true, "http", "[::1]", UrlHostKind::Ipv6},
        CefUrlAdapterCase{"Ipv6Compression", "https://[2001:DB8:0:0:0:0:0:1]/path",
            true, "https", "[2001:db8::1]", UrlHostKind::Ipv6},
        CefUrlAdapterCase{"InternationalDomain", "https://b\xC3\xBC" "cher.de/path",
            true, "https", "xn--bcher-kva.de", UrlHostKind::Domain},
        CefUrlAdapterCase{"AboutBlank", "about:blank",
            true, "about", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"DataScheme", "data:text/plain,example",
            true, "data", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"FileScheme", "file:///C:/example.txt",
            true, "file", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"EmptyUrl", "",
            false, "", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"MissingHost", "https://",
            false, "", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"MalformedIpv6", "https://[invalid]/path",
            false, "", "", UrlHostKind::Unknown},
        CefUrlAdapterCase{"InvalidPort", "https://example.com:65536/path",
            false, "", "", UrlHostKind::Unknown}),
    // Los nombres identifican los datos del caso sin conservar referencias externas.
    [](const ::testing::TestParamInfo<CefUrlAdapterCase>& info) {
        return std::string(info.param.name);
    });

}  // namespace sbp
