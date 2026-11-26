#include "auth_refresh_test.h"
#include "rest/security_event_serializer.h"

#include <memory>
#include <optional>
#include <utility>

namespace sbp {

class AuthSecurityEventTest : public AuthRefreshTest {
protected:
    const SecurityContext origin{6, 17, 3};
    const SecurityEvent event{"10ea1c30-82a3-4590-9fec-66d778ea21cf", SecurityEventType::UrlBlocked,
                              {}, R"({"schemaVersion":1,"reason":"DOMAIN_DENIED","host":"blocked.example"})"};
    const QByteArray route = "POST /api/v1/devices/17/events/batch";
    const QByteArray confirmed = R"({"acceptedEventUuids":["10ea1c30-82a3-4590-9fec-66d778ea21cf"]})";

    std::shared_ptr<std::optional<AuthResult>> SendEvent();
};

// La respuesta permanece viva hasta que Qt libera el callback, incluso si la prueba falla antes.
std::shared_ptr<std::optional<AuthResult>> AuthSecurityEventTest::SendEvent() {
    auto result = std::make_shared<std::optional<AuthResult>>();
    // Contrastamos el contexto original tanto en las confirmaciones como en los errores.
    session->SendSecurityEvent(origin, event, [result, expected = origin](AuthResult received, SecurityContext context) {
        EXPECT_EQ(expected, context);
        EXPECT_FALSE(result->has_value());
        *result = std::move(received);
    });
    return result;
}

// El primer POST y su único reintento envían exactamente el mismo lote al mismo dispositivo.
TEST_F(AuthSecurityEventTest, RefreshesBearerAndResendsIdenticalBatch) {
    const auto result = SendEvent();
    ASSERT_TRUE(WaitForRequest(1, route));
    const QByteArray first = server.Requests()[1];
    const auto separator = first.indexOf("\r\n\r\n");
    ASSERT_GE(separator, 0);
    EXPECT_TRUE(first.left(separator + 2).toLower().contains("\r\nauthorization: bearer fake-access\r\n"));
    const QByteArray body = first.mid(separator + 4);
    EXPECT_EQ(SecurityEventSerializer::Serialize(event), body);
    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(401, R"({"code":"AUTHENTICATION_REQUIRED"})")));
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    ASSERT_TRUE(WaitForRequest(3, route));
    const QByteArray retry = server.Requests()[3];
    const auto retrySeparator = retry.indexOf("\r\n\r\n");
    ASSERT_GE(retrySeparator, 0);
    EXPECT_TRUE(retry.left(retrySeparator + 2).toLower().contains("\r\nauthorization: bearer new-access\r\n"));
    EXPECT_EQ(body, retry.mid(retrySeparator + 4));

    ASSERT_TRUE(server.ReplyToHeldRequest(3, HttpResponse(200, confirmed)));
    // Procesamos los eventos de Qt hasta recibir la confirmación de ese UUID.
    ASSERT_TRUE(WaitFor([&result] { return result->has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, result->value().outcome);
    EXPECT_EQ(200, result->value().statusCode);
    EXPECT_EQ(4u, server.Requests().size());
}

// Un 200 con otro UUID y un 409 de conflicto se comunican como fallos distintos.
TEST_F(AuthSecurityEventTest, DistinguishesWrongConfirmationFromUuidConflict) {
    const auto wrong = SendEvent();
    ASSERT_TRUE(WaitForRequest(1, route));
    const QByteArray other = R"({"acceptedEventUuids":["47a98de0-214a-438d-af9f-b52999e9e8b0"]})";
    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(200, other)));
    // El POST ha respondido, pero el cuerpo no confirma el evento enviado.
    ASSERT_TRUE(WaitFor([&wrong] { return wrong->has_value(); }));
    EXPECT_EQ(AuthOutcome::InvalidResponse, wrong->value().outcome);

    const auto conflict = SendEvent();
    ASSERT_TRUE(WaitForRequest(2, route));
    ASSERT_TRUE(server.ReplyToHeldRequest(2, HttpResponse(409, R"({"code":"EVENT_UUID_CONFLICT"})")));
    // El conflicto del evento conserva su código de Problem Details.
    ASSERT_TRUE(WaitFor([&conflict] { return conflict->has_value(); }));
    EXPECT_EQ(AuthOutcome::HttpError, conflict->value().outcome);
    EXPECT_EQ(409, conflict->value().statusCode);
    EXPECT_EQ(QStringLiteral("EVENT_UUID_CONFLICT"), conflict->value().problemCode);
    EXPECT_EQ(3u, server.Requests().size());
}

// Un 401 tardío de un evento anterior al nuevo login no puede reenviar el lote con el nuevo Bearer.
TEST_F(AuthSecurityEventTest, DiscardsLate401AfterNewLogin) {
    const auto late = SendEvent();
    ASSERT_TRUE(WaitForRequest(1, route));
    ASSERT_NO_FATAL_FAILURE(LoginAs(QStringLiteral("other@example.test"), ValidRefreshResponse));
    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(401, R"({"code":"AUTHENTICATION_REQUIRED"})")));
    // Qt ya ha descartado la entrega cuando solo el test conserva su referencia al resultado.
    ASSERT_TRUE(WaitFor([&late] { return late.use_count() == 1; }));
    EXPECT_FALSE(late->has_value());
    EXPECT_EQ(3u, server.Requests().size());
}

}  // namespace sbp
