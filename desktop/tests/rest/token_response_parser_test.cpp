#include "rest/token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <gtest/gtest.h>

namespace sbp {

// Tokens ficticios: estas pruebas validan el DTO, no la firma de un JWT ni una sesión del backend.
constexpr char ValidTokenResponseJson[] =
    R"({"accessToken":"header.payload.signature","tokenType":"Bearer","expiresIn":900,"refreshToken":"refresh-fixture"})";

// Comprueba que el lector conserva los cuatro campos y no publica una respuesta parcial.
TEST(TokenResponseParserTest, ReadsCompleteResponseAndPreservesAllFields) {
    const auto result = TokenResponseParser::Parse(ValidTokenResponseJson);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(QStringLiteral("header.payload.signature"), result->accessToken);
    EXPECT_EQ(QStringLiteral("Bearer"), result->tokenType);
    EXPECT_EQ(900, result->expiresIn);
    EXPECT_EQ(QStringLiteral("refresh-fixture"), result->refreshToken);
}

// Detecta un posible truncamiento a 32 bits y mantiene la comparación de Bearer sin distinguir mayúsculas.
TEST(TokenResponseParserTest, AcceptsCaseInsensitiveBearerAnd64BitDuration) {
    const QByteArray body =
        R"({"accessToken":"access-fixture","tokenType":"bEaReR","expiresIn":5000000001,"refreshToken":"refresh-fixture"})";
    const auto result = TokenResponseParser::Parse(body);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(QStringLiteral("bEaReR"), result->tokenType);
    EXPECT_EQ(5'000'000'001LL, result->expiresIn);
}

// Incluye cuerpo vacío, JSON incompleto, raíces distintas de objeto y contenido posterior al objeto.
TEST(TokenResponseParserTest, RejectsMalformedJsonAndNonObjectRoots) {
    const QByteArray invalidBodies[] = {
        {}, "{", "[]", "null", "true", "\"text\"", QByteArray(ValidTokenResponseJson) + " trailing"
    };
    int index = 0;
    for (const auto& body : invalidBodies) {
        SCOPED_TRACE(index++);
        EXPECT_FALSE(TokenResponseParser::Parse(body).has_value());
    }
}

// Cada campo debe existir y conservar su tipo; un valor por defecto no puede ocultar su ausencia.
TEST(TokenResponseParserTest, RejectsMissingNullAndWronglyTypedFields) {
    const QJsonObject valid = QJsonDocument::fromJson(ValidTokenResponseJson).object();
    const QString fields[] = {
        QStringLiteral("accessToken"), QStringLiteral("tokenType"),
        QStringLiteral("expiresIn"), QStringLiteral("refreshToken")
    };
    for (const auto& field : fields) {
        SCOPED_TRACE(field.toStdString());
        QJsonObject missing = valid;
        missing.remove(field);
        EXPECT_FALSE(TokenResponseParser::Parse(QJsonDocument(missing).toJson()).has_value());

        QJsonObject nullField = valid;
        nullField.insert(field, QJsonValue(QJsonValue::Null));
        EXPECT_FALSE(TokenResponseParser::Parse(QJsonDocument(nullField).toJson()).has_value());

        QJsonObject wrongType = valid;
        // La duración como texto "900" debe rechazarse; en los campos de texto se inserta un número.
        wrongType.insert(field, field == QStringLiteral("expiresIn")
            ? QJsonValue(QStringLiteral("900")) : QJsonValue(123));
        EXPECT_FALSE(TokenResponseParser::Parse(QJsonDocument(wrongType).toJson()).has_value());
    }
}

// Un campo de texto puede tener el tipo correcto y aun así carecer de contenido o usar otro esquema.
TEST(TokenResponseParserTest, RejectsBlankTokensAndUnsupportedScheme) {
    const QJsonObject valid = QJsonDocument::fromJson(ValidTokenResponseJson).object();
    const QString tokenFields[] = {QStringLiteral("accessToken"), QStringLiteral("refreshToken")};
    const QString blankValues[] = {QString{}, QStringLiteral(" \t\r\n")};
    for (const auto& field : tokenFields) {
        SCOPED_TRACE(field.toStdString());
        for (const auto& value : blankValues) {
            QJsonObject object = valid;
            object.insert(field, value);
            EXPECT_FALSE(TokenResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
        }
    }

    const QString invalidSchemes[] = {QString{}, QStringLiteral("Basic"), QStringLiteral(" Bearer ")};
    for (const auto& scheme : invalidSchemes) {
        QJsonObject object = valid;
        object.insert(QStringLiteral("tokenType"), scheme);
        EXPECT_FALSE(TokenResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
    }
}

// Usa números JSON literales para comprobar fracciones y desbordamientos sin conversiones previas del test.
TEST(TokenResponseParserTest, RejectsNonPositiveFractionalAndOutOfRangeDuration) {
    const QByteArray invalidDurations[] = {"0", "-1", "0.5", "900.5", "9223372036854775808", "1e100"};
    for (const auto& duration : invalidDurations) {
        SCOPED_TRACE(duration.constData());
        QByteArray body(ValidTokenResponseJson);
        body.replace("\"expiresIn\":900", "\"expiresIn\":" + duration);
        EXPECT_FALSE(TokenResponseParser::Parse(body).has_value());
    }
}

}  // namespace sbp
