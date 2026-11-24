#include "rest/user_identity_parser.h"

#include <gtest/gtest.h>

namespace sbp {

// Comprueba que el lector encuentra id y companyId por su nombre aunque cambie el orden o haya otros campos.
TEST(UserIdentityParserTest, ReadsUserAndCompanyIdsFromProfile) {
    const QByteArray body = R"({"companyId":3,"name":"Ana","role":"USER","id":7})";
    const auto result = UserIdentityParser::Parse(body);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(7, result->id);
    EXPECT_EQ(3, result->companyId);
}

// Comprueba que ambos identificadores conservan su valor exacto al leer enteros grandes de 64 bits.
TEST(UserIdentityParserTest, Preserves64BitIntegerIds) {
    // El primer entero supera la precisión exacta de double y el segundo es el máximo Long de Java.
    const auto result = UserIdentityParser::Parse(R"({"id":9007199254740993,"companyId":9223372036854775807})");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(9'007'199'254'740'993LL, result->id);
    EXPECT_EQ(9'223'372'036'854'775'807LL, result->companyId);
}

// Comprueba que se rechaza el cuerpo si el JSON está mal formado, la raíz es un array o falta id o companyId.
TEST(UserIdentityParserTest, RejectsInvalidDocumentsAndMissingIds) {
    const QByteArray bodies[] = {
        "", "{", R"([{"id":7,"companyId":3}])", "{}", R"({"id":7})", R"({"companyId":3})",
        R"({"userId":7,"companyId":3})"
    };
    for (const auto& body : bodies) {
        SCOPED_TRACE(body.constData());
        EXPECT_FALSE(UserIdentityParser::Parse(body).has_value());
    }
}

// Comprueba por separado que id y companyId solo aceptan enteros positivos dentro del rango de 64 bits con signo.
TEST(UserIdentityParserTest, RejectsInvalidValuesInEitherId) {
    const QByteArray invalidValues[] = {R"("7")", "null", "true", "0", "-1", "3.5", "9223372036854775808"};
    for (const auto& value : invalidValues) {
        SCOPED_TRACE(value.constData());
        const QByteArray invalidUser = "{\"id\":" + value + ",\"companyId\":3}";
        const QByteArray invalidCompany = "{\"id\":7,\"companyId\":" + value + "}";
        EXPECT_FALSE(UserIdentityParser::Parse(invalidUser).has_value());
        EXPECT_FALSE(UserIdentityParser::Parse(invalidCompany).has_value());
    }
}

}  // namespace sbp
