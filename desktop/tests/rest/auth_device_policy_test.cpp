#include "auth_device_policy_test.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>
#include <thread>
#include <utility>

namespace sbp {

const SecurityContext PolicyContext(6, 24, 3);
constexpr char DevicePolicyPath[] = "/api/v1/devices/24/policy";
constexpr char AuthenticationRequiredBody[] = R"({"code":"AUTHENTICATION_REQUIRED"})";
constexpr char AssignedPolicyBody[] = R"({
    "id":12, "companyId":3, "name":"Laboratorio",
    "urlFilteringEnabled":true, "urlFilteringMode":"ALLOWLIST", "urlRules":["example.org"],
    "downloadControlEnabled":true, "downloadControlMode":"DENYLIST", "downloadRules":["exe"],
    "createdAt":"2026-11-25T10:00:00Z", "updatedAt":"2026-11-25T11:30:00Z"
})";

// El callback conserva el resultado aunque el test termine antes de que el cliente libere la petición.
std::shared_ptr<AuthDevicePolicyTest::PolicyResult> AuthDevicePolicyTest::FetchPolicy(SecurityContext context) {
    auto reply = std::make_shared<PolicyResult>();
    // Cada petición guarda su propio contexto para detectar una entrega al receptor equivocado.
    session->FetchDevicePolicy(context, [reply, context](AuthResult received, SecurityContext receivedContext,
                                                        std::optional<PolicySnapshot> policy) {
        EXPECT_EQ(context, receivedContext);
        EXPECT_EQ(0, reply->calls);
        ++reply->calls;
        reply->result = std::move(received);
        reply->policy = std::move(policy);
    });
    return reply;
}

// Compara el resultado entregado por el callback con la respuesta esperada para este fallo.
void AuthDevicePolicyTest::ExpectPolicyError(const std::shared_ptr<PolicyResult>& reply, AuthOutcome outcome,
                                            std::optional<int> status, RestTransportError transport) {
    // Atiende los eventos de Qt hasta que la petición termina o vence la espera del test.
    ASSERT_TRUE(WaitFor([&reply] { return reply->calls != 0; }));
    EXPECT_EQ(1, reply->calls);
    EXPECT_EQ(outcome, reply->result.outcome);
    EXPECT_EQ(status, reply->result.statusCode);
    EXPECT_EQ(transport, reply->result.transportError);
    EXPECT_FALSE(reply->policy.has_value());
}

// Comprueba la ruta y el Bearer del GET y que el callback recibe la política con el contexto que la solicitó.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyReturnsPolicyWithOriginalContext) {
    const SecurityContext context(1, 17, 3);
    const auto reply = FetchPolicy(context);

    const QByteArray body = R"({
        "id":5, "companyId":3, "name":"Aula",
        "urlFilteringEnabled":true, "urlFilteringMode":"DENYLIST", "urlRules":["example.org"],
        "downloadControlEnabled":false, "downloadControlMode":"ALLOWLIST", "downloadRules":[],
        "createdAt":"2026-11-25T10:00:00Z", "updatedAt":"2026-11-25T10:30:00Z"
    })";
    // La petición 0 fue el login. La petición 1 debe usar el ID del dispositivo y el token de ese login.
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, "/api/v1/devices/17/policy", "fake-access", 200, body));
    // Atiende los eventos Qt hasta que el callback guarda el resultado.
    ASSERT_TRUE(WaitFor([&reply] { return reply->calls != 0; }));
    EXPECT_EQ(1, reply->calls);
    EXPECT_EQ(AuthOutcome::Success, reply->result.outcome);
    EXPECT_EQ(200, reply->result.statusCode);
    ASSERT_TRUE(reply->policy.has_value());
    EXPECT_EQ(5, reply->policy->Id());
    EXPECT_EQ(context.CompanyId(), reply->policy->CompanyId());
    EXPECT_EQ("Aula", reply->policy->Name());
}

// Un callback vacío, una generación cero, IDs inválidos u otro hilo se rechazan antes del GET.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyRejectsInvalidCalls) {
    EXPECT_THROW(session->FetchDevicePolicy(PolicyContext, {}), std::invalid_argument);
    for (const auto& context : {SecurityContext(0, 24, 3), SecurityContext(6, 0, 3), SecurityContext(6, 24, -1)}) {
        EXPECT_THROW((void)FetchPolicy(context), std::invalid_argument);
    }
    // join mantiene viva la sesión hasta que se comprueba el rechazo en el otro hilo.
    std::thread otherThread([this] {
        EXPECT_THROW((void)FetchPolicy(PolicyContext), std::logic_error);
    });
    otherThread.join();
    ProcessEventsFor(50);
    EXPECT_EQ(1u, server.Requests().size());
}

// Un HTTP 200 con un documento incompleto o con una política válida de otra empresa se entrega sin snapshot.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyRejectsInvalidOrForeignPolicy) {
    auto foreignPolicy = QJsonDocument::fromJson(AssignedPolicyBody).object();
    foreignPolicy.insert(QStringLiteral("companyId"), 4);
    const QByteArray bodies[] = {"{}", QJsonDocument(foreignPolicy).toJson(QJsonDocument::Compact)};
    for (const auto& body : bodies) {
        SCOPED_TRACE(body.constData());
        const auto index = server.Requests().size();
        const auto reply = FetchPolicy(PolicyContext);
        ASSERT_TRUE(ReplyToAuthenticatedGet(index, DevicePolicyPath, "fake-access", 200, body));
        ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(reply, AuthOutcome::InvalidResponse, 200));
    }
}

// Solo 403, 404 y 409 DEVICE_INACTIVE del GET retiran el acceso a la política de este contexto.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyClassifiesErrorsFromTheGet) {
    struct Case {
        int status;
        const char* code;
        AuthOutcome expected;
    };
    const Case cases[] = {
        {403, nullptr, AuthOutcome::DeviceAccessDenied}, {404, "DEVICE_NOT_FOUND", AuthOutcome::DeviceAccessDenied},
        {409, "DEVICE_INACTIVE", AuthOutcome::DeviceAccessDenied}, {409, "OTHER_CONFLICT", AuthOutcome::HttpError},
        {503, "SERVICE_UNAVAILABLE", AuthOutcome::HttpError}, {401, "INVALID_REFRESH_TOKEN", AuthOutcome::HttpError}
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(::testing::Message() << entry.status << " " << (entry.code ? entry.code : ""));
        QJsonObject problem;
        std::optional<QString> expectedCode;
        if (entry.code) {
            expectedCode = QString::fromLatin1(entry.code);
            problem.insert(QStringLiteral("code"), *expectedCode);
        }
        const auto index = server.Requests().size();
        const auto reply = FetchPolicy(PolicyContext);
        ASSERT_TRUE(ReplyToAuthenticatedGet(index, DevicePolicyPath, "fake-access", entry.status, QJsonDocument(problem).toJson()));
        ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(reply, entry.expected, entry.status));
        EXPECT_EQ(expectedCode, reply->result.problemCode);
        EXPECT_EQ(index + 1, server.Requests().size());
    }
}

// Un GET interrumpido antes de recibir el cuerpo completo conserva el estado HTTP y comunica el fallo de transporte.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyReportsInterruptedResponse) {
    const auto reply = FetchPolicy(PolicyContext);
    ASSERT_TRUE(WaitForRequest(1, "GET /api/v1/devices/24/policy"));
    ASSERT_TRUE(server.ReplyToHeldRequest(1, "HTTP/1.1 200 OK\r\nContent-Length: 80\r\nConnection: close\r\n\r\n{"));
    ExpectPolicyError(reply, AuthOutcome::TransportError, 200, RestTransportError::Network);
}

// El 401 de la política inicia un refresh y el reintento pide el mismo dispositivo con el Bearer renovado.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyRetriesWithNewBearer) {
    const auto reply = FetchPolicy(PolicyContext);
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 401, AuthenticationRequiredBody));
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    EXPECT_EQ(0, reply->calls);
    ASSERT_TRUE(ReplyToAuthenticatedGet(3, DevicePolicyPath, "new-access", 200, AssignedPolicyBody));
    // El callback original debe recibir la política del reintento junto con el contexto original.
    ASSERT_TRUE(WaitFor([&reply] { return reply->calls != 0; }));
    EXPECT_EQ(AuthOutcome::Success, reply->result.outcome);
    EXPECT_EQ(200, reply->result.statusCode);
    ASSERT_TRUE(reply->policy.has_value());
    EXPECT_EQ(12, reply->policy->Id());
    EXPECT_EQ("Laboratorio", reply->policy->Name());
    EXPECT_EQ(4u, server.Requests().size());
}

// Un segundo 401 de la política termina el intento y llega al callback como error HTTP del GET.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyStopsAfterSecond401) {
    const auto reply = FetchPolicy(PolicyContext);
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 401, AuthenticationRequiredBody));
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    ASSERT_TRUE(ReplyToAuthenticatedGet(3, DevicePolicyPath, "new-access", 401, AuthenticationRequiredBody));
    ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(reply, AuthOutcome::HttpError, 401));
    EXPECT_EQ(QStringLiteral("AUTHENTICATION_REQUIRED"), reply->result.problemCode);
    EXPECT_EQ(4u, server.Requests().size());
}

// Los mismos códigos emitidos por el POST de refresh conservan su origen y la sesión puede volver a consultar.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyKeepsSessionAfterUnconfirmedRefreshErrors) {
    for (int status : {401, 403, 404, 409, 503}) {
        SCOPED_TRACE(status);
        const auto index = server.Requests().size();
        const auto reply = FetchPolicy(PolicyContext);
        ASSERT_TRUE(ReplyToAuthenticatedGet(index, DevicePolicyPath, "fake-access", 401, AuthenticationRequiredBody));
        const QByteArray body = status == 409 ? R"({"code":"DEVICE_INACTIVE"})" : "{}";
        ASSERT_TRUE(ReplyToRefresh(index + 1, HttpResponse(status, body), QStringLiteral("fake-refresh")));
        ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(reply, AuthOutcome::HttpError, status));
        EXPECT_EQ(index + 2, server.Requests().size());
    }
    const auto index = server.Requests().size();
    const auto recovered = FetchPolicy(PolicyContext);
    ASSERT_TRUE(ReplyToAuthenticatedGet(index, DevicePolicyPath, "fake-access", 200, AssignedPolicyBody));
    // El mismo login todavía permite recibir una política tras el fallo temporal.
    ASSERT_TRUE(WaitFor([&recovered] { return recovered->calls != 0; }));
    EXPECT_EQ(AuthOutcome::Success, recovered->result.outcome);
    EXPECT_TRUE(recovered->policy.has_value());
}

// Solo el rechazo confirmado del refreshToken entrega NoSession; la consulta posterior termina localmente.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyReportsNoSessionAfterRejectedRefreshToken) {
    const auto reply = FetchPolicy(PolicyContext);
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 401, AuthenticationRequiredBody));
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(401, R"({"code":"INVALID_REFRESH_TOKEN"})"), QStringLiteral("fake-refresh")));
    ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(reply, AuthOutcome::NoSession, 401));
    EXPECT_EQ(QStringLiteral("INVALID_REFRESH_TOKEN"), reply->result.problemCode);
    const auto local = FetchPolicy(PolicyContext);
    ASSERT_EQ(1, local->calls);
    ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(local, AuthOutcome::NoSession, std::nullopt));
    EXPECT_FALSE(local->result.problemCode.has_value());
    EXPECT_EQ(3u, server.Requests().size());
}

// Dos consultas del mismo login que terminan en orden inverso conservan el contexto de cada instalación.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyKeepsContextsOnOutOfOrderReplies) {
    const auto first = FetchPolicy(PolicyContext);
    ASSERT_TRUE(WaitForRequest(1, "GET /api/v1/devices/24/policy"));
    const auto second = FetchPolicy(SecurityContext(7, 31, 3));
    ASSERT_TRUE(ReplyToAuthenticatedGet(2, "/api/v1/devices/31/policy", "fake-access", 200, AssignedPolicyBody));
    // El éxito de la segunda consulta llega mientras el servidor aún retiene el GET de la primera.
    ASSERT_TRUE(WaitFor([&second] { return second->calls != 0; }));
    EXPECT_EQ(AuthOutcome::Success, second->result.outcome);
    EXPECT_TRUE(second->policy.has_value());
    EXPECT_EQ(0, first->calls);
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 403, "{}"));
    ASSERT_NO_FATAL_FAILURE(ExpectPolicyError(first, AuthOutcome::DeviceAccessDenied, 403));
    EXPECT_EQ(1, second->calls);
}

// Incluso con la misma cuenta y el mismo dispositivo, otro login retira tanto el éxito como la denegación tardíos.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyDiscardsLateRepliesAfterNewLogin) {
    const auto lateSuccess = FetchPolicy(PolicyContext);
    ASSERT_TRUE(WaitForRequest(1, "GET /api/v1/devices/24/policy"));
    const auto lateDenial = FetchPolicy(PolicyContext);
    ASSERT_TRUE(WaitForRequest(2, "GET /api/v1/devices/24/policy"));
    ASSERT_NO_FATAL_FAILURE(LoginAs(QStringLiteral("user@example.test"), ValidRefreshResponse));
    const auto current = FetchPolicy(SecurityContext(7, 24, 3));
    ASSERT_TRUE(ReplyToAuthenticatedGet(4, DevicePolicyPath, "new-access", 200, AssignedPolicyBody));
    ASSERT_TRUE(ReplyToAuthenticatedGet(2, DevicePolicyPath, "fake-access", 404, "{}"));
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 200, AssignedPolicyBody));
    // Al liberarse los receptores antiguos sabemos que el transporte ya procesó sus dos respuestas.
    ASSERT_TRUE(WaitFor([&] { return current->calls != 0 && lateSuccess.use_count() == 1 && lateDenial.use_count() == 1; }));
    EXPECT_EQ(0, lateSuccess->calls);
    EXPECT_EQ(0, lateDenial->calls);
    EXPECT_EQ(AuthOutcome::Success, current->result.outcome);
    EXPECT_TRUE(current->policy.has_value());
}

// Destruir la sesión durante el refresh retira el callback de política antes de que llegue el rechazo del POST.
TEST_F(AuthDevicePolicyTest, FetchDevicePolicyDiscardsRefreshAfterSessionDestruction) {
    const auto pending = FetchPolicy(PolicyContext);
    ASSERT_TRUE(ReplyToAuthenticatedGet(1, DevicePolicyPath, "fake-access", 401, AuthenticationRequiredBody));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    session.reset();
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(401, R"({"code":"INVALID_REFRESH_TOKEN"})"), QStringLiteral("fake-refresh")));
    // El resultado queda en manos del test cuando la lambda de red libera su copia.
    ASSERT_TRUE(WaitFor([&pending] { return pending.use_count() == 1; }));
    EXPECT_EQ(0, pending->calls);
    EXPECT_EQ(3u, server.Requests().size());
}

}  // namespace sbp
