#include "rest/security_event_serializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <gtest/gtest.h>

#include <chrono>
#include <string>

namespace sbp {

// Comprueba el lote de un bloqueo tal como lo leería el backend, incluido el JSON interno como cadena.
TEST(SecurityEventSerializerTest, SerializesUrlBlockedInSingleEventBatch) {
    using namespace std::chrono_literals;
    const auto occurredAt = std::chrono::sys_days{2026y/11/26} + 9h + 15min + 30s + 123456us;
    const std::string details = R"({"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7,"host":"blocked.example"})";
    const SecurityEvent event{"10ea1c30-82a3-4590-9fec-66d778ea21cf", SecurityEventType::UrlBlocked, occurredAt, details};

    const QByteArray body = SecurityEventSerializer::Serialize(event);
    const QJsonDocument document = QJsonDocument::fromJson(body);
    ASSERT_TRUE(document.isObject());
    const QJsonObject batch = document.object();
    ASSERT_EQ(1, batch.size());
    const QJsonArray events = batch.value(QStringLiteral("events")).toArray();
    ASSERT_EQ(1, events.size());

    const QJsonObject item = events.at(0).toObject();
    ASSERT_EQ(4, item.size());
    EXPECT_EQ(QString::fromStdString(event.eventUuid), item.value(QStringLiteral("eventUuid")).toString());
    EXPECT_EQ(QStringLiteral("URL_BLOCKED"), item.value(QStringLiteral("type")).toString());
    EXPECT_EQ(QStringLiteral("2026-11-26T09:15:30.123456Z"), item.value(QStringLiteral("occurredAt")).toString());
    EXPECT_EQ(QString::fromStdString(details), item.value(QStringLiteral("details")).toString());
}

}  // namespace sbp
