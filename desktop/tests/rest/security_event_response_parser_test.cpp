#include "rest/security_event_response_parser.h"

#include <gtest/gtest.h>

namespace sbp {

// El backend reconoce el mismo UUID aunque normalice el hexadecimal a minúsculas.
TEST(SecurityEventResponseParserTest, ConfirmsOnlyTheSentUuid) {
    const QByteArray body = R"({"acceptedEventUuids":["10ea1c30-82a3-4590-9fec-66d778ea21cf"]})";
    EXPECT_TRUE(SecurityEventResponseParser::ConfirmsEvent(body, "10EA1C30-82A3-4590-9FEC-66D778EA21CF"));
    EXPECT_FALSE(SecurityEventResponseParser::ConfirmsEvent(body, "47a98de0-214a-438d-af9f-b52999e9e8b0"));
}

// Una respuesta incompleta, mal formada o con elementos adicionales no confirma un lote de un evento.
TEST(SecurityEventResponseParserTest, RejectsMalformedOrUnexpectedAcknowledgements) {
    const std::string uuid = "10ea1c30-82a3-4590-9fec-66d778ea21cf";
    for (const char* body : {"{", "{}", R"({"acceptedEventUuids":[]})", R"({"acceptedEventUuids":[null]})",
                             R"({"acceptedEventUuids":"10ea1c30-82a3-4590-9fec-66d778ea21cf"})",
                             R"({"acceptedEventUuids":["10ea1c30-82a3-4590-9fec-66d778ea21cf","47a98de0-214a-438d-af9f-b52999e9e8b0"]})"}) {
        EXPECT_FALSE(SecurityEventResponseParser::ConfirmsEvent(body, uuid)) << body;
    }
    EXPECT_FALSE(SecurityEventResponseParser::ConfirmsEvent(R"({"acceptedEventUuids":["abc"]})", "abc"));
}

}  // namespace sbp
