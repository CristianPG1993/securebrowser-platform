#include "rest/device_identity_parser.h"

#include <QJsonDocument>
#include <QJsonValue>
#include <QVariant>

#include <gtest/gtest.h>

namespace sbp {

// Un elemento de items. Los tres IDs tienen valores distintos para detectar intercambios entre campos.
constexpr char DeviceItemJson[] = R"({
    "id": 9007199254740993,
    "deviceIdentifier": "b7eacb45-4aba-4a5a-8c0c-633a61294af1",
    "userId": 5000000001,
    "companyId": 9223372036854775807,
    "active": true
})";

// Lee los cinco campos por su nombre y conserva los IDs de 64 bits aunque el backend incluya otros datos.
TEST(DeviceIdentityParserTest, ReadsDeviceIdentityWith64BitIds) {
    QJsonObject item = QJsonDocument::fromJson(DeviceItemJson).object();
    item.insert(QStringLiteral("name"), QStringLiteral("Desktop Demo"));
    const auto device = DeviceIdentityParser::Parse(item);

    ASSERT_TRUE(device.has_value());
    EXPECT_EQ(9'007'199'254'740'993LL, device->id);
    EXPECT_EQ(QStringLiteral("b7eacb45-4aba-4a5a-8c0c-633a61294af1"), device->deviceIdentifier);
    EXPECT_EQ(5'000'000'001LL, device->userId);
    EXPECT_EQ(9'223'372'036'854'775'807LL, device->companyId);
    EXPECT_TRUE(device->active);
}

// Acepta el booleano false para un dispositivo inactivo y rechaza el texto "false" y el número 0.
TEST(DeviceIdentityParserTest, RequiresBooleanActiveAndPreservesFalse) {
    QJsonObject item = QJsonDocument::fromJson(DeviceItemJson).object();
    item.insert(QStringLiteral("active"), false);
    const auto device = DeviceIdentityParser::Parse(item);
    ASSERT_TRUE(device.has_value());
    EXPECT_FALSE(device->active);

    item.insert(QStringLiteral("active"), QStringLiteral("false"));
    EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
    item.insert(QStringLiteral("active"), 0);
    EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
}

// Cada uno de los cinco campos debe estar presente y tener un valor distinto de null.
TEST(DeviceIdentityParserTest, RejectsMissingAndNullFields) {
    const QJsonObject valid = QJsonDocument::fromJson(DeviceItemJson).object();
    for (const auto& field : valid.keys()) {
        SCOPED_TRACE(field.toStdString());
        QJsonObject item = valid;
        item.remove(field);
        EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
        item.insert(field, QJsonValue::Null);
        EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
    }
}

// Comprueba los límites y el tipo de cada identificador numérico por separado.
TEST(DeviceIdentityParserTest, RejectsInvalidNumericIds) {
    const QJsonObject valid = QJsonDocument::fromJson(DeviceItemJson).object();
    const QString fields[] = {QStringLiteral("id"), QStringLiteral("userId"), QStringLiteral("companyId")};
    // El último número supera en uno el máximo de un entero de 64 bits con signo.
    const QJsonValue invalidValues[] = {QStringLiteral("17"), true, 0, -1, 3.5, 9'223'372'036'854'775'808.0};
    for (const auto& field : fields) {
        SCOPED_TRACE(field.toStdString());
        for (const auto& value : invalidValues) {
            SCOPED_TRACE(value.toVariant().toString().toStdString());
            QJsonObject item = valid;
            item.insert(field, value);
            EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
        }
    }
}

// Rechaza los identificadores que no respetan el formato UUID canónico del backend.
TEST(DeviceIdentityParserTest, RejectsNonCanonicalDeviceIdentifiers) {
    QJsonObject item = QJsonDocument::fromJson(DeviceItemJson).object();
    const QString uuid = item.value(QStringLiteral("deviceIdentifier")).toString();
    const QJsonValue invalidValues[] = {
        17, QStringLiteral(""), uuid.toUpper(), QStringLiteral("{") + uuid + QStringLiteral("}"),
        uuid.sliced(1), uuid + QStringLiteral(" "), QStringLiteral("z") + uuid.sliced(1)
    };
    for (const auto& value : invalidValues) {
        SCOPED_TRACE(value.toVariant().toString().toStdString());
        item.insert(QStringLiteral("deviceIdentifier"), value);
        EXPECT_FALSE(DeviceIdentityParser::Parse(item).has_value());
    }
}

}  // namespace sbp
