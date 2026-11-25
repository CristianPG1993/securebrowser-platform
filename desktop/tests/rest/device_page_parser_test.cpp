#include "rest/device_page_parser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

namespace sbp {

// Primera página de un listado de tres dispositivos, con dos elementos por página.
constexpr char FirstDevicePageJson[] = R"({
    "page": 0, "size": 2, "totalElements": 3, "totalPages": 2,
    "items": [
        {"id":17,"deviceIdentifier":"b7eacb45-4aba-4a5a-8c0c-633a61294af1","userId":7,"companyId":3,"active":true},
        {"id":24,"deviceIdentifier":"d25b8f06-c2c4-4e04-a0b9-82a10fb9443b","userId":7,"companyId":3,"active":false}
    ]
})";

// Comprueba el índice, el total de páginas y el orden de los dos dispositivos recibidos.
TEST(DevicePageParserTest, ReadsFirstPageInOrder) {
    const auto result = DevicePageParser::Parse(FirstDevicePageJson);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(0, result->page);
    EXPECT_EQ(2, result->totalPages);
    ASSERT_EQ(2u, result->items.size());
    EXPECT_EQ(17, result->items[0].id);
    EXPECT_TRUE(result->items[0].active);
    EXPECT_EQ(24, result->items[1].id);
    EXPECT_FALSE(result->items[1].active);
}

// Con 21 dispositivos y 10 por página, la página 2 contiene únicamente el último dispositivo.
TEST(DevicePageParserTest, ReadsPartialLastPage) {
    const auto result = DevicePageParser::Parse(R"({
        "page": 2, "size": 10, "totalElements": 21, "totalPages": 3,
        "items": [{"id":45,"deviceIdentifier":"b7eacb45-4aba-4a5a-8c0c-633a61294af1","userId":7,"companyId":3,"active":true}]
    })");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(2, result->page);
    EXPECT_EQ(3, result->totalPages);
    ASSERT_EQ(1u, result->items.size());
    EXPECT_EQ(45, result->items[0].id);
}

// Tanto un listado vacío como una página posterior a la última producen una página válida sin dispositivos.
TEST(DevicePageParserTest, AcceptsEmptyListingAndPagePastEnd) {
    const auto empty = DevicePageParser::Parse(R"({"page":0,"size":20,"totalElements":0,"totalPages":0,"items":[]})");
    ASSERT_TRUE(empty.has_value());
    EXPECT_EQ(0, empty->page);
    EXPECT_EQ(0, empty->totalPages);
    EXPECT_TRUE(empty->items.empty());

    const auto pastEnd = DevicePageParser::Parse(R"({"page":3,"size":10,"totalElements":21,"totalPages":3,"items":[]})");
    ASSERT_TRUE(pastEnd.has_value());
    EXPECT_EQ(3, pastEnd->page);
    EXPECT_EQ(3, pastEnd->totalPages);
    EXPECT_TRUE(pastEnd->items.empty());
}

// Rechaza raíces inválidas y comprueba que cada campo sea obligatorio y tenga el tipo esperado.
TEST(DevicePageParserTest, RejectsInvalidDocumentsAndFields) {
    EXPECT_FALSE(DevicePageParser::Parse("{").has_value());
    EXPECT_FALSE(DevicePageParser::Parse("[]").has_value());
    const QJsonObject valid = QJsonDocument::fromJson(FirstDevicePageJson).object();
    for (const auto& field : valid.keys()) {
        SCOPED_TRACE(field.toStdString());
        QJsonObject page = valid;
        page.remove(field);
        EXPECT_FALSE(DevicePageParser::Parse(QJsonDocument(page).toJson()).has_value());
        page.insert(field, QStringLiteral("2"));
        EXPECT_FALSE(DevicePageParser::Parse(QJsonDocument(page).toJson()).has_value());
    }
}

// Rechaza tamaños fuera de rango, recuentos contradictorios y páginas con más o menos items de los anunciados.
TEST(DevicePageParserTest, RejectsInvalidPagination) {
    const QByteArray invalidPages[] = {
        R"({"page":-1,"size":20,"totalElements":0,"totalPages":0,"items":[]})",
        R"({"page":0,"size":0,"totalElements":0,"totalPages":0,"items":[]})",
        R"({"page":0,"size":101,"totalElements":0,"totalPages":0,"items":[]})",
        R"({"page":0,"size":10,"totalElements":-1,"totalPages":0,"items":[]})",
        R"({"page":0,"size":10,"totalElements":0,"totalPages":-1,"items":[]})",
        R"({"page":3,"size":10,"totalElements":21,"totalPages":2,"items":[]})",
        R"({"page":2,"size":10,"totalElements":21,"totalPages":3,"items":[]})"
    };
    for (const auto& body : invalidPages) {
        SCOPED_TRACE(body.constData());
        EXPECT_FALSE(DevicePageParser::Parse(body).has_value());
    }
    QJsonObject page = QJsonDocument::fromJson(FirstDevicePageJson).object();
    page.insert(QStringLiteral("page"), 1);
    EXPECT_FALSE(DevicePageParser::Parse(QJsonDocument(page).toJson()).has_value());
}

// Un segundo item inválido descarta toda la página aunque el primero ya se haya leído correctamente.
TEST(DevicePageParserTest, RejectsWholePageIfSecondItemIsInvalid) {
    QJsonObject page = QJsonDocument::fromJson(FirstDevicePageJson).object();
    QJsonArray items = page.value(QStringLiteral("items")).toArray();
    QJsonObject incomplete = items.at(1).toObject();
    incomplete.remove(QStringLiteral("deviceIdentifier"));
    const QJsonValue invalidItems[] = {QStringLiteral("texto"), incomplete};
    for (const auto& invalidItem : invalidItems) {
        items.replace(1, invalidItem);
        page.insert(QStringLiteral("items"), items);
        EXPECT_FALSE(DevicePageParser::Parse(QJsonDocument(page).toJson()).has_value());
    }
}

}  // namespace sbp
