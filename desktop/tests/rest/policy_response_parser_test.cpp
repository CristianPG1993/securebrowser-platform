#include "rest/policy_response_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <vector>

namespace sbp {

constexpr char ValidPolicyResponse[] = R"({
    "id": 9007199254740993, "companyId": 3, "name": "Aula",
    "urlFilteringEnabled": true, "urlFilteringMode": "DENYLIST",
    "urlRules": ["example.com", "example.org"],
    "downloadControlEnabled": false, "downloadControlMode": "ALLOWLIST",
    "downloadRules": ["pdf", "zip"],
    "createdAt": "2026-11-25T10:30:00.12Z",
    "updatedAt": "2026-11-25T10:30:01.654321Z"
})";

// Comprueba los once campos, incluidas las reglas del control desactivado y las dos fechas UTC.
TEST(PolicyResponseParserTest, ReadsCompleteAssignedPolicy) {
    const auto policy = PolicyResponseParser::Parse(ValidPolicyResponse);
    ASSERT_TRUE(policy.has_value());
    EXPECT_EQ(9'007'199'254'740'993LL, policy->Id());
    EXPECT_EQ(3, policy->CompanyId());
    EXPECT_EQ("Aula", policy->Name());

    const std::vector<std::string> domains{"example.com", "example.org"};
    EXPECT_TRUE(policy->UrlFiltering().Enabled());
    EXPECT_EQ(FilterMode::Denylist, policy->UrlFiltering().Mode());
    EXPECT_EQ(domains, policy->UrlFiltering().Rules());

    const std::vector<std::string> extensions{"pdf", "zip"};
    EXPECT_FALSE(policy->DownloadControl().Enabled());
    EXPECT_EQ(FilterMode::Allowlist, policy->DownloadControl().Mode());
    EXPECT_EQ(extensions, policy->DownloadControl().Rules());

    using namespace std::chrono_literals;
    const auto baseTime = std::chrono::sys_days{2026y/11/25} + 10h + 30min;
    EXPECT_EQ(baseTime + 120000us, policy->CreatedAt());
    EXPECT_EQ(baseTime + 1s + 654321us, policy->UpdatedAt());
}

// Un documento debe ser un objeto y los once campos deben estar presentes y tener un valor distinto de null.
TEST(PolicyResponseParserTest, RejectsMalformedDocumentsAndMissingOrNullFields) {
    for (const char* body : {"", "{", "[]", "{}"}) {
        EXPECT_FALSE(PolicyResponseParser::Parse(body).has_value()) << body;
    }

    const QJsonObject valid = QJsonDocument::fromJson(ValidPolicyResponse).object();
    for (const QString& field : valid.keys()) {
        SCOPED_TRACE(field.toStdString());
        QJsonObject object = valid;
        object.remove(field);
        EXPECT_FALSE(PolicyResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
        object.insert(field, QJsonValue::Null);
        EXPECT_FALSE(PolicyResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
    }
}

// Tanto el ID de la política como el de la empresa deben ser enteros positivos de 64 bits.
TEST(PolicyResponseParserTest, RejectsInvalidIdentifiers) {
    const QJsonValue invalidIds[] = {0, -1, 1.5, 9'223'372'036'854'775'808.0, QStringLiteral("3"), true};
    for (const QString& field : {QStringLiteral("id"), QStringLiteral("companyId")}) {
        QJsonObject object = QJsonDocument::fromJson(ValidPolicyResponse).object();
        for (const QJsonValue& id : invalidIds) {
            object.insert(field, id);
            const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
            EXPECT_FALSE(PolicyResponseParser::Parse(body).has_value()) << body.constData();
        }
    }
}

// Rechaza nombres vacíos o de otro tipo, indicadores que imitan un booleano y modos ajenos al contrato.
TEST(PolicyResponseParserTest, RejectsInvalidNameFlagsAndModes) {
    struct InvalidField {
        const char* field;
        QJsonValue value;
    };
    const InvalidField cases[] = {
        {"name", QStringLiteral("")}, {"name", 17},
        {"urlFilteringEnabled", QStringLiteral("true")}, {"downloadControlEnabled", 0},
        {"urlFilteringMode", QStringLiteral("denylist")}, {"downloadControlMode", QStringLiteral("UNKNOWN")}
    };
    const QJsonObject valid = QJsonDocument::fromJson(ValidPolicyResponse).object();
    for (const auto& entry : cases) {
        QJsonObject object = valid;
        object.insert(QString::fromLatin1(entry.field), entry.value);
        const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
        EXPECT_FALSE(PolicyResponseParser::Parse(body).has_value()) << body.constData();
    }
}

// Una lista vacía es válida incluso con el control activo. El nombre conserva sus bytes UTF-8.
TEST(PolicyResponseParserTest, PreservesEmptyRuleListsAndUtf8Name) {
    QJsonObject object = QJsonDocument::fromJson(ValidPolicyResponse).object();
    object.insert(QStringLiteral("name"), QStringLiteral("Pol\u00edtica de aula"));
    object.insert(QStringLiteral("urlRules"), QJsonArray{});
    object.insert(QStringLiteral("downloadRules"), QJsonArray{});
    const auto policy = PolicyResponseParser::Parse(QJsonDocument(object).toJson());
    ASSERT_TRUE(policy.has_value());
    EXPECT_EQ("Pol\xc3\xadtica de aula", policy->Name());
    EXPECT_TRUE(policy->UrlFiltering().Rules().empty());
    EXPECT_TRUE(policy->DownloadControl().Rules().empty());
}

// Una cadena suelta o un segundo elemento numérico invalidan toda la lista en cualquiera de los dos controles.
TEST(PolicyResponseParserTest, RejectsInvalidRuleLists) {
    const QJsonObject valid = QJsonDocument::fromJson(ValidPolicyResponse).object();
    for (const QString& field : {QStringLiteral("urlRules"), QStringLiteral("downloadRules")}) {
        SCOPED_TRACE(field.toStdString());
        QJsonObject object = valid;
        QJsonArray rules = object.value(field).toArray();
        object.insert(field, rules.at(0));
        EXPECT_FALSE(PolicyResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
        rules.replace(1, 7);
        object.insert(field, rules);
        EXPECT_FALSE(PolicyResponseParser::Parse(QJsonDocument(object).toJson()).has_value());
    }
}

// Acepta el segundo entero y el día bisiesto. La fracción de nueve cifras se trunca al microsegundo.
TEST(PolicyResponseParserTest, ReadsUtcSecondsAndTruncatesNanoseconds) {
    QJsonObject object = QJsonDocument::fromJson(ValidPolicyResponse).object();
    object.insert(QStringLiteral("createdAt"), QStringLiteral("2024-02-29T00:00:00Z"));
    object.insert(QStringLiteral("updatedAt"), QStringLiteral("2024-02-29T00:00:00.123456999+00:00"));
    const auto policy = PolicyResponseParser::Parse(QJsonDocument(object).toJson());
    ASSERT_TRUE(policy.has_value());
    using namespace std::chrono_literals;
    const auto midnight = std::chrono::sys_days{2024y/2/29};
    EXPECT_EQ(midnight, policy->CreatedAt());
    EXPECT_EQ(midnight + 123456us, policy->UpdatedAt());
}

// Ambas fechas requieren un instante real, zona UTC explícita y una fracción válida si aparece el punto.
TEST(PolicyResponseParserTest, RejectsInvalidTimestamps) {
    const QJsonValue invalidDates[] = {
        123, QStringLiteral("2025-02-29T10:30:00Z"), QStringLiteral("2026-11-25T25:30:00Z"),
        QStringLiteral("2026-11-25T10:30:00"), QStringLiteral("2026-11-25T10:30:00+01:00"),
        QStringLiteral("2026-11-25T10:30:00.Z"), QStringLiteral("2026-11-25T10:30:00.1234567890Z"),
        QStringLiteral("2026-11-25T10:30:00Zextra")
    };
    for (const QString& field : {QStringLiteral("createdAt"), QStringLiteral("updatedAt")}) {
        QJsonObject object = QJsonDocument::fromJson(ValidPolicyResponse).object();
        for (const QJsonValue& date : invalidDates) {
            object.insert(field, date);
            const QByteArray body = QJsonDocument(object).toJson(QJsonDocument::Compact);
            EXPECT_FALSE(PolicyResponseParser::Parse(body).has_value()) << body.constData();
        }
    }
}

}  // namespace sbp
