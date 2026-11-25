#include "auth_refresh_test.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

namespace sbp {

constexpr char AuthenticationRequired[] = R"({"code":"AUTHENTICATION_REQUIRED"})";
constexpr char InvalidRefreshToken[] = R"({"code":"INVALID_REFRESH_TOKEN"})";
constexpr char ValidProfile[] = R"({"id":7,"companyId":3})";

// Abre el puerto de prueba, conecta AuthSession a esa URL y completa el login inicial.
void AuthRefreshTest::SetUp() {
    ASSERT_TRUE(server.Listen());
    session = std::make_unique<AuthSession>(client, server.Url(QStringLiteral("/api/v1/")));
    LoginAs(QStringLiteral("user@example.test"), ValidLoginResponse);
}

// Contesta al POST de login con un 200 y comprueba que la callback de la sesión recibe Success.
void AuthRefreshTest::LoginAs(const QString& email, const QByteArray& responseBody) {
    const auto index = server.Requests().size();
    auto result = std::make_shared<std::optional<AuthOutcome>>();
    // El resultado vive hasta que el cliente haya liberado la callback, incluso si la prueba falla antes.
    session->Login(email, QStringLiteral("clave"), [result](AuthResult received) {
        *result = received.outcome;
    });
    ASSERT_TRUE(WaitForRequest(index, "POST /api/v1/auth/login"));
    ASSERT_TRUE(server.ReplyToHeldRequest(index, HttpResponse(200, responseBody)));
    // El nuevo login debe estar aceptado antes de continuar en el hilo Qt.
    ASSERT_TRUE(WaitFor([&result] { return result->has_value(); }));
    ASSERT_EQ(AuthOutcome::Success, result->value());
}

// Permite al test leer la identidad entregada por FetchUserIdentity y contar las llamadas a su callback.
std::shared_ptr<AuthRefreshTest::ProfileResult> AuthRefreshTest::FetchProfile() {
    auto profile = std::make_shared<ProfileResult>();
    // Cada llamada captura su propio resultado para detectar entregas duplicadas o de otra consulta.
    session->FetchUserIdentity([profile](AuthResult received, std::optional<UserIdentity> identity) {
        ++profile->calls;
        profile->result = std::move(received);
        profile->identity = std::move(identity);
    });
    return profile;
}

// Atiende los eventos de Qt hasta recibir la petición y contrasta su primera línea con methodAndPath.
bool AuthRefreshTest::WaitForRequest(std::size_t index, const QByteArray& methodAndPath) {
    // El índice identifica la petición incluso cuando ya han llegado otras al servidor de prueba.
    return WaitFor([this, index] { return server.Requests().size() > index; })
        && server.Requests()[index].startsWith(methodAndPath + " HTTP/1.1\r\n");
}

// Lee Authorization del GET retenido y envía la respuesta simulada si coinciden la ruta y el token.
bool AuthRefreshTest::ReplyToAuthenticatedGet(std::size_t index, const QByteArray& path, const QByteArray& accessToken,
                                             int status, const QByteArray& body) {
    if (!WaitForRequest(index, "GET " + path)) {
        return false;
    }
    const QByteArray& request = server.Requests()[index];
    const QByteArray name = "\r\nauthorization:";
    const auto start = request.toLower().indexOf(name);
    return start >= 0 && request.sliced(start + name.size()).startsWith(" Bearer " + accessToken + "\r\n")
        && server.ReplyToHeldRequest(index, HttpResponse(status, body));
}

// Aplica la misma comprobación del Bearer a la ruta fija del perfil.
bool AuthRefreshTest::ReplyToProfile(std::size_t index, const QByteArray& accessToken, int status, const QByteArray& body) {
    return ReplyToAuthenticatedGet(index, "/api/v1/users/me", accessToken, status, body);
}

// Extrae el cuerpo del POST /auth/refresh y comprueba sus campos antes de devolver la respuesta preparada.
bool AuthRefreshTest::ReplyToRefresh(std::size_t index, const QByteArray& response, const QString& refreshToken) {
    if (!WaitForRequest(index, "POST /api/v1/auth/refresh")) {
        return false;
    }
    const QByteArray& request = server.Requests()[index];
    const auto separator = request.indexOf("\r\n\r\n");
    const QJsonObject expected{{QStringLiteral("refreshToken"), refreshToken}};
    return separator >= 0 && QJsonDocument::fromJson(request.mid(separator + 4)).object() == expected
        && server.ReplyToHeldRequest(index, response);
}

// Dos GET rechazados y una consulta iniciada mientras se renueva comparten un único POST de refresh.
TEST_F(AuthRefreshTest, ConcurrentProfileRequestsShareOneRefresh) {
    const auto first = FetchProfile();
    ASSERT_TRUE(WaitForRequest(1, "GET /api/v1/users/me"));
    const auto second = FetchProfile();
    ASSERT_TRUE(WaitForRequest(2, "GET /api/v1/users/me"));
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(WaitForRequest(3, "POST /api/v1/auth/refresh"));
    const auto third = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(2, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(ReplyToRefresh(3, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));

    for (std::size_t index = 4; index <= 6; ++index) {
        ASSERT_TRUE(ReplyToProfile(index, "new-access", 200, ValidProfile));
    }
    // Cada consulta original debe recibir exactamente una identidad.
    ASSERT_TRUE(WaitFor([&] { return first->calls && second->calls && third->calls; }));
    for (const auto& profile : {first, second, third}) {
        EXPECT_EQ(1, profile->calls);
        EXPECT_EQ(AuthOutcome::Success, profile->result.outcome);
        ASSERT_TRUE(profile->identity.has_value());
        EXPECT_EQ(7, profile->identity->id);
        EXPECT_EQ(3, profile->identity->companyId);
    }
    EXPECT_EQ(7u, server.Requests().size());
}

// Un 401 del par anterior reutiliza la renovación ya terminada aunque el accessToken tenga el mismo texto.
TEST_F(AuthRefreshTest, Late401UsesCurrentTokenRevision) {
    const auto first = FetchProfile();
    ASSERT_TRUE(WaitForRequest(1, "GET /api/v1/users/me"));
    const auto late = FetchProfile();
    ASSERT_TRUE(WaitForRequest(2, "GET /api/v1/users/me"));
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    const QByteArray rotated = R"({"accessToken":"fake-access","tokenType":"Bearer","expiresIn":900,"refreshToken":"rotated"})";
    ASSERT_TRUE(ReplyToRefresh(3, HttpResponse(200, rotated), QStringLiteral("fake-refresh")));
    ASSERT_TRUE(ReplyToProfile(4, "fake-access", 200, ValidProfile));
    // Deja pendiente el GET 2 hasta confirmar que el nuevo par ya está guardado.
    ASSERT_TRUE(WaitFor([&first] { return first->calls == 1; }));
    ASSERT_EQ(AuthOutcome::Success, first->result.outcome);
    ASSERT_TRUE(ReplyToProfile(2, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(ReplyToProfile(5, "fake-access", 200, ValidProfile));
    // El 401 tardío solo ha ocasionado el GET 5.
    ASSERT_TRUE(WaitFor([&late] { return late->calls == 1; }));
    EXPECT_EQ(AuthOutcome::Success, late->result.outcome);
    EXPECT_EQ(6u, server.Requests().size());

    // Una renovación posterior sí debe enviar el refreshToken rotado.
    const auto next = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(6, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(ReplyToRefresh(7, HttpResponse(200, ValidRefreshResponse), QStringLiteral("rotated")));
    ASSERT_TRUE(ReplyToProfile(8, "new-access", 200, ValidProfile));
    // Comprueba que también se publicó el nuevo accessToken de esta segunda renovación.
    ASSERT_TRUE(WaitFor([&next] { return next->calls == 1; }));
    EXPECT_EQ(AuthOutcome::Success, next->result.outcome);
}

// Un rechazo explícito llega a los receptores y deja las siguientes consultas en NoSession.
TEST_F(AuthRefreshTest, InvalidRefreshTokenEndsSessionForAllWaiters) {
    const auto first = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    const auto waiting = FetchProfile();
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(401, InvalidRefreshToken), QStringLiteral("fake-refresh")));
    // Ambos receptores reciben el rechazo del POST, sin identidad.
    ASSERT_TRUE(WaitFor([&] { return first->calls && waiting->calls; }));
    for (const auto& profile : {first, waiting}) {
        EXPECT_EQ(1, profile->calls);
        EXPECT_EQ(AuthOutcome::HttpError, profile->result.outcome);
        EXPECT_EQ(401, profile->result.statusCode);
        EXPECT_EQ(QStringLiteral("INVALID_REFRESH_TOKEN"), profile->result.problemCode);
        EXPECT_FALSE(profile->identity.has_value());
    }
    const auto next = FetchProfile();
    ASSERT_EQ(1, next->calls);
    EXPECT_EQ(AuthOutcome::NoSession, next->result.outcome);
    EXPECT_FALSE(next->identity.has_value());
    EXPECT_EQ(3u, server.Requests().size());
}

// Los fallos temporales y las respuestas de renovación inválidas conservan ambos tokens del login.
TEST_F(AuthRefreshTest, FailedRefreshPreservesTokensAndAllowsAnotherAttempt) {
    struct Case {
        const char* name;
        QByteArray response;
        int status;
        AuthOutcome outcome;
        RestTransportError transport;
    };
    const Case cases[] = {
        {"503", HttpResponse(503, "{}"), 503, AuthOutcome::HttpError, RestTransportError::None},
        {"401 sin rechazo del refreshToken", HttpResponse(401, "{}"), 401, AuthOutcome::HttpError, RestTransportError::None},
        {"JSON incompleto", HttpResponse(200, R"({"accessToken":"partial"})"), 200, AuthOutcome::InvalidResponse, RestTransportError::None},
        {"refreshToken repetido", HttpResponse(200, ValidLoginResponse), 200, AuthOutcome::InvalidResponse, RestTransportError::None},
        {"estado inesperado", HttpResponse(201, ValidRefreshResponse), 201, AuthOutcome::InvalidResponse, RestTransportError::None},
        {"cuerpo truncado", "HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n{", 200,
            AuthOutcome::TransportError, RestTransportError::Network},
    };
    for (const auto& entry : cases) {
        SCOPED_TRACE(entry.name);
        const auto start = server.Requests().size();
        const auto profile = FetchProfile();
        ASSERT_TRUE(ReplyToProfile(start, "fake-access", 401, AuthenticationRequired));
        ASSERT_TRUE(ReplyToRefresh(start + 1, entry.response, QStringLiteral("fake-refresh")));
        // El fallo se entrega al llamador y termina este intento.
        ASSERT_TRUE(WaitFor([&profile] { return profile->calls == 1; }));
        EXPECT_EQ(entry.status, profile->result.statusCode);
        EXPECT_EQ(entry.outcome, profile->result.outcome);
        EXPECT_EQ(entry.transport, profile->result.transportError);
        EXPECT_FALSE(profile->identity.has_value());
        EXPECT_EQ(start + 2, server.Requests().size());
    }
    const auto start = server.Requests().size();
    const auto recovered = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(start, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(ReplyToRefresh(start + 1, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    ASSERT_TRUE(ReplyToProfile(start + 2, "new-access", 200, ValidProfile));
    // Tras el fallo de transporte también se puede iniciar una consulta independiente y recuperar el perfil.
    ASSERT_TRUE(WaitFor([&recovered] { return recovered->calls == 1; }));
    EXPECT_EQ(AuthOutcome::Success, recovered->result.outcome);
}

// Un error del perfil que no sea 401 AUTHENTICATION_REQUIRED se entrega directamente al receptor.
TEST_F(AuthRefreshTest, OtherProfileErrorsDoNotStartRefresh) {
    const std::pair<int, const char*> errors[] = {
        {401, "INVALID_CREDENTIALS"}, {403, "ACCESS_DENIED"}, {404, "RESOURCE_NOT_FOUND"},
        {409, "DEVICE_INACTIVE"}, {503, "SERVICE_UNAVAILABLE"}, {403, "AUTHENTICATION_REQUIRED"},
    };
    for (const auto& [status, code] : errors) {
        SCOPED_TRACE(code);
        const auto index = server.Requests().size();
        const auto profile = FetchProfile();
        const QByteArray body = QJsonDocument(QJsonObject{{QStringLiteral("code"), QString::fromLatin1(code)}})
            .toJson(QJsonDocument::Compact);
        ASSERT_TRUE(ReplyToProfile(index, "fake-access", status, body));
        // El receptor obtiene el error del GET original con su código HTTP.
        ASSERT_TRUE(WaitFor([&profile] { return profile->calls == 1; }));
        EXPECT_EQ(AuthOutcome::HttpError, profile->result.outcome);
        EXPECT_EQ(status, profile->result.statusCode);
        EXPECT_EQ(QString::fromLatin1(code), profile->result.problemCode);
        EXPECT_FALSE(profile->identity.has_value());
        EXPECT_EQ(index + 1, server.Requests().size());
    }
}

// Cancelar el POST avisa a las consultas en espera y permite volver a usar el par original.
TEST_F(AuthRefreshTest, CancelledRefreshPreservesSession) {
    const auto first = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    const auto waiting = FetchProfile();
    client.CancelAll();
    // Ambos receptores deben saber que su operación se canceló.
    ASSERT_TRUE(WaitFor([&] { return first->calls && waiting->calls; }));
    for (const auto& profile : {first, waiting}) {
        EXPECT_EQ(1, profile->calls);
        EXPECT_EQ(AuthOutcome::TransportError, profile->result.outcome);
        EXPECT_EQ(RestTransportError::Cancelled, profile->result.transportError);
        EXPECT_FALSE(profile->identity.has_value());
    }
    const auto current = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(3, "fake-access", 200, ValidProfile));
    // La consulta posterior usa el token conservado y completa con normalidad.
    ASSERT_TRUE(WaitFor([&current] { return current->calls == 1; }));
    EXPECT_EQ(AuthOutcome::Success, current->result.outcome);
    EXPECT_EQ(4u, server.Requests().size());
}

// Un refresh de un login anterior no puede cambiar los tokens ni completar consultas de la sesión sustituida.
TEST_F(AuthRefreshTest, NewLoginDiscardsLateRefreshSuccessAndRejection) {
    for (int status : {200, 401}) {
        SCOPED_TRACE(status);
        const auto start = server.Requests().size();
        const auto old = FetchProfile();
        ASSERT_TRUE(ReplyToProfile(start, "fake-access", 401, AuthenticationRequired));
        ASSERT_TRUE(WaitForRequest(start + 1, "POST /api/v1/auth/refresh"));
        const auto waiting = FetchProfile();
        ASSERT_NO_FATAL_FAILURE(LoginAs(QStringLiteral("second@example.test"), ValidLoginResponse));
        const QByteArray body = status == 200 ? ValidRefreshResponse : InvalidRefreshToken;
        ASSERT_TRUE(ReplyToRefresh(start + 1, HttpResponse(status, body), QStringLiteral("fake-refresh")));
        // Cuando se liberan ambas callbacks antiguas podemos comprobar que ninguna recibió la entrega.
        ASSERT_TRUE(WaitFor([&] { return old.use_count() == 1 && waiting.use_count() == 1; }));
        EXPECT_EQ(0, old->calls);
        EXPECT_EQ(0, waiting->calls);
        const auto current = FetchProfile();
        ASSERT_TRUE(ReplyToProfile(start + 3, "fake-access", 200, ValidProfile));
        // El éxito o el rechazo del refresh antiguo no ha sustituido ni borrado los tokens del login actual.
        ASSERT_TRUE(WaitFor([&current] { return current->calls == 1; }));
        EXPECT_EQ(AuthOutcome::Success, current->result.outcome);
    }
}

// Al destruir la sesión se descartan las consultas en espera aunque el servidor complete el refresh.
TEST_F(AuthRefreshTest, DestroyedSessionDiscardsRefreshAndWaiters) {
    const auto first = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    const auto waiting = FetchProfile();
    session.reset();
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    // El transporte sigue vivo y procesa la respuesta antes de que se inspeccionen los receptores.
    ASSERT_TRUE(WaitFor([&] { return first.use_count() == 1 && waiting.use_count() == 1; }));
    EXPECT_EQ(0, first->calls);
    EXPECT_EQ(0, waiting->calls);
    EXPECT_EQ(3u, server.Requests().size());
}

// Si el primer receptor destruye la sesión al recibir un fallo, el segundo queda retirado.
TEST_F(AuthRefreshTest, FirstWaiterMayDestroySessionDuringFailureDelivery) {
    // Esta callback se ejecuta en el hilo Qt de la prueba durante la entrega del error del refresh.
    session->FetchUserIdentity([this](AuthResult result, std::optional<UserIdentity> identity) {
        EXPECT_EQ(AuthOutcome::HttpError, result.outcome);
        EXPECT_EQ(503, result.statusCode);
        EXPECT_FALSE(identity.has_value());
        session.reset();
    });
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, AuthenticationRequired));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    const auto waiting = FetchProfile();
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(503, "{}"), QStringLiteral("fake-refresh")));
    // Espera a que se hayan retirado las callbacks y comprueba que solo se ejecutó la primera.
    ASSERT_TRUE(WaitFor([&] { return !session && waiting.use_count() == 1; }));
    EXPECT_EQ(0, waiting->calls);
    EXPECT_EQ(3u, server.Requests().size());
}

}  // namespace sbp
