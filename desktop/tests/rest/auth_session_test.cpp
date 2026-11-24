#include "rest_test_support.h"

#include "rest/auth_session.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>

namespace sbp {

// Reutiliza el servidor y la espera de eventos de las pruebas de transporte.
using AuthSessionTest = RestClientTest;

constexpr char ValidLoginResponse[] =
    R"({"accessToken":"fake-access","tokenType":"Bearer","expiresIn":900,"refreshToken":"fake-refresh"})";

TEST_F(AuthSessionTest, LoginPostsCredentialsAndReportsSuccess) {
    RestTestServer server(HttpResponse(200, ValidLoginResponse));
    ASSERT_TRUE(server.Listen());

    std::optional<AuthResult> result;
    RestClient client;
    // La base termina en /v1 para comprobar que Login conserva ese segmento al construir el endpoint.
    AuthSession session(client, server.Url(QStringLiteral("/api/v1")));
    const QString email = QStringLiteral("user@example.test");
    const QString password = QStringLiteral("  clave \"de prueba\"  ");
    // La callback escribe en result en el hilo Qt de la prueba. result vive más que session y client.
    session.Login(email, password, [&result](AuthResult received) {
        result = std::move(received);
    });
    // WaitFor atiende los eventos del cliente y del servidor hasta que la callback entrega el resultado.
    ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, result->outcome);
    EXPECT_EQ(200, result->statusCode);

    ASSERT_EQ(1u, server.Requests().size());
    const QByteArray& request = server.Requests().front();
    EXPECT_TRUE(request.startsWith("POST /api/v1/auth/login HTTP/1.1\r\n"));
    const qsizetype separator = request.indexOf("\r\n\r\n");
    ASSERT_GE(separator, 0);
    const QByteArray headers = request.first(separator + 2).toLower();
    EXPECT_TRUE(headers.contains("\r\ncontent-type: application/json\r\n"));
    EXPECT_FALSE(headers.contains("\r\nauthorization:"));

    // Compara los campos tal como los recibió el servidor, incluidos los espacios y las comillas de la clave.
    const QJsonDocument body = QJsonDocument::fromJson(request.mid(separator + 4));
    const QJsonObject expected{{QStringLiteral("email"), email}, {QStringLiteral("password"), password}};
    EXPECT_EQ(expected, body.object());
}

TEST_F(AuthSessionTest, LoginReportsInvalidCredentialsOn401) {
    RestTestServer server(HttpResponse(401, R"({"code":"INVALID_CREDENTIALS"})"));
    ASSERT_TRUE(server.Listen());

    std::optional<AuthResult> result;
    RestClient client;
    AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
    // La callback guarda el resultado en el hilo Qt mientras result sigue vivo.
    session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave-incorrecta"), [&result](AuthResult received) {
        result = std::move(received);
    });
    // El 401 también completa la petición. WaitFor termina cuando la callback entrega ese resultado.
    ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
    EXPECT_EQ(AuthOutcome::HttpError, result->outcome);
    EXPECT_EQ(401, result->statusCode);
    EXPECT_EQ(QStringLiteral("INVALID_CREDENTIALS"), result->problemCode);
    EXPECT_EQ(RestTransportError::None, result->transportError);
}

TEST_F(AuthSessionTest, LoginRequiresHttp200AndCompleteTokens) {
    // El primer 200 tiene un DTO incompleto. El 201 sí trae tokens válidos, pero el login exige un 200.
    const std::pair<int, QByteArray> cases[] = {
        {200, R"({"accessToken":"fake-access"})"}, {201, ValidLoginResponse}
    };
    for (const auto& [status, body] : cases) {
        SCOPED_TRACE(status);
        RestTestServer server(HttpResponse(status, body));
        ASSERT_TRUE(server.Listen());
        std::optional<AuthResult> result;
        RestClient client;
        AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
        // Guarda en esta iteración lo que Login haya interpretado del estado y del cuerpo.
        session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&result](AuthResult received) {
            result = std::move(received);
        });
        // Procesa los eventos en el mismo hilo Qt que la sesión.
        ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
        EXPECT_EQ(AuthOutcome::InvalidResponse, result->outcome);
        EXPECT_EQ(status, result->statusCode);
        EXPECT_EQ(RestTransportError::None, result->transportError);
        EXPECT_FALSE(result->problemCode.has_value());
    }
}

TEST_F(AuthSessionTest, LoginPreservesHttpErrorWithoutAUsableProblemCode) {
    const QByteArray bodies[] = {"Service unavailable", R"({})", R"({"code":42})", R"({"code":"  "})"};
    for (const auto& body : bodies) {
        SCOPED_TRACE(body.toStdString());
        RestTestServer server(HttpResponse(503, body));
        ASSERT_TRUE(server.Listen());
        std::optional<AuthResult> result;
        RestClient client;
        AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
        // Guarda el error que Login entrega incluso cuando el cuerpo del 503 no aporta un código útil.
        session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&result](AuthResult received) {
            result = std::move(received);
        });
        // El resultado de la callback sigue accesible hasta terminar esta iteración.
        ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
        EXPECT_EQ(AuthOutcome::HttpError, result->outcome);
        EXPECT_EQ(503, result->statusCode);
        EXPECT_FALSE(result->problemCode.has_value());
        EXPECT_EQ(RestTransportError::None, result->transportError);
    }
}

TEST_F(AuthSessionTest, LoginReportsDroppedConnectionBeforeOrAfterHttpStatus) {
    // El segundo caso envía JSON válido, pero anuncia un byte de más y corta el cuerpo HTTP.
    const QByteArray body = ValidLoginResponse;
    const QByteArray truncated = "HTTP/1.1 200 OK\r\nContent-Length: "
        + QByteArray::number(body.size() + 1) + "\r\nConnection: close\r\n\r\n" + body;
    for (const auto mode : {RestTestServer::Mode::Disconnect, RestTestServer::Mode::Reply}) {
        const bool receivedStatus = mode == RestTestServer::Mode::Reply;
        SCOPED_TRACE(receivedStatus);
        RestTestServer server(truncated, mode);
        ASSERT_TRUE(server.Listen());
        std::optional<AuthResult> result;
        RestClient client;
        AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
        // Guarda el fallo de red tal como lo recibe quien pidió el login.
        session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&result](AuthResult received) {
            result = std::move(received);
        });
        // La espera deja que Qt detecte el cierre del socket en ambos casos.
        ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
        EXPECT_EQ(AuthOutcome::TransportError, result->outcome);
        EXPECT_EQ(RestTransportError::Network, result->transportError);
        EXPECT_EQ(receivedStatus ? std::optional<int>(200) : std::nullopt, result->statusCode);
        EXPECT_FALSE(result->problemCode.has_value());
    }
}

TEST_F(AuthSessionTest, NewerLoginDiscardsLateSuccessAndLateError) {
    for (const int oldStatus : {200, 401}) {
        SCOPED_TRACE(oldStatus);
        RestTestServer server({}, RestTestServer::Mode::Hold);
        ASSERT_TRUE(server.Listen());
        const auto oldCalls = std::make_shared<int>(0);
        std::optional<AuthResult> result;
        RestClient client;
        AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
        // El test y la callback antigua comparten el contador hasta que el transporte retire esa entrega.
        session.Login(QStringLiteral("first@example.test"), QStringLiteral("clave"), [oldCalls](AuthResult) {
            ++*oldCalls;
        });
        // Asegura que el índice 0 del servidor corresponde al primer intento.
        ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
        // El segundo intento es el vigente y debe completar aunque el primero siga pendiente.
        session.Login(QStringLiteral("second@example.test"), QStringLiteral("clave"), [&result](AuthResult received) {
            result = std::move(received);
        });
        // Retenemos ambos POST antes de elegir el orden de las respuestas.
        ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 2; }));
        ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(200, ValidLoginResponse)));
        // Confirma que el segundo login ya tuvo éxito antes de liberar el primero.
        ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
        ASSERT_EQ(AuthOutcome::Success, result->outcome);

        const QByteArray oldBody = oldStatus == 200 ? ValidLoginResponse : R"({"code":"INVALID_CREDENTIALS"})";
        ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(oldStatus, oldBody)));
        // Cuando solo el test conserva el contador, la callback antigua ya se ha liberado.
        ASSERT_TRUE(WaitFor([&oldCalls] { return oldCalls.use_count() == 1; }));
        EXPECT_EQ(0, *oldCalls);
    }
}

TEST_F(AuthSessionTest, DestroyedSessionDiscardsPendingLoginResponse) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    const auto calls = std::make_shared<int>(0);
    RestClient client;
    {
        AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
        // El contador vive también en el test para observar qué ocurre después de destruir session.
        session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [calls](AuthResult) {
            ++*calls;
        });
        // El servidor ha recibido el POST mientras session sigue viva.
        ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
    }
    // El transporte continúa vivo y recibe el 200 cuando la sesión ya se ha destruido.
    ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(200, ValidLoginResponse)));
    // Espera la retirada efectiva de la callback en el hilo Qt antes de leer su contador.
    ASSERT_TRUE(WaitFor([&calls] { return calls.use_count() == 1; }));
    EXPECT_EQ(0, *calls);
}

TEST_F(AuthSessionTest, LoginRejectsInvalidApiBasesBeforeSendingCredentials) {
    RestTestServer server(HttpResponse(200, ValidLoginResponse));
    ASSERT_TRUE(server.Listen());
    const QUrl base = server.Url(QStringLiteral("/api/v1/"));
    QUrl withQuery(base);
    withQuery.setQuery(QStringLiteral("key=value"));
    QUrl withFragment(base);
    withFragment.setFragment(QStringLiteral("fragment"));
    QUrl withUser(base);
    withUser.setUserName(QStringLiteral("user"));
    const QUrl invalidBases[] = {
        QUrl(QStringLiteral("/api/v1/")), QUrl(QStringLiteral("http://127.0.0.2/api/v1/")),
        withQuery, withFragment, withUser
    };
    int calls = 0;
    RestClient client;
    // Una base inválida debe lanzar la excepción durante la llamada a Login.
    const auto completed = [&calls](AuthResult) { ++calls; };
    for (const auto& invalidBase : invalidBases) {
        SCOPED_TRACE(invalidBase.toString().toStdString());
        AuthSession session(client, invalidBase);
        EXPECT_THROW(session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), completed),
                     std::invalid_argument);
    }
    ProcessEventsFor(50);
    EXPECT_TRUE(server.Requests().empty());
    EXPECT_EQ(0, calls);
}

TEST_F(AuthSessionTest, InvalidCallsPreserveTheLoginAlreadyInProgress) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    std::optional<AuthResult> result;
    RestClient client;
    AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));
    const QString email = QStringLiteral("user@example.test");
    const QString password = QStringLiteral("clave");
    // Solo el intento válido debe llegar al servidor y entregar un resultado en el hilo Qt.
    const auto completed = [&result](AuthResult received) { result = std::move(received); };
    session.Login(email, password, completed);
    // El primer intento ya está pendiente antes de probar los dos usos incorrectos de la sesión.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
    EXPECT_THROW(session.Login(email, password, {}), std::invalid_argument);
    // join mantiene vivas las referencias y el hilo dueño espera antes de procesar más eventos Qt.
    std::thread otherThread([&] {
        EXPECT_THROW(session.Login(email, password, completed), std::logic_error);
    });
    otherThread.join();

    ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(200, ValidLoginResponse)));
    // Si una llamada rechazada hubiera avanzado la generación, este resultado se descartaría.
    ASSERT_TRUE(WaitFor([&result] { return result.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, result->outcome);
    EXPECT_EQ(1u, server.Requests().size());
}

// Comprueba que FetchUserIdentity usa el token de Login en GET /users/me y entrega los dos IDs del perfil.
TEST_F(AuthSessionTest, FetchUserIdentityUsesLoginTokenAndReadsProfile) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    std::optional<AuthResult> loginResult;
    std::optional<AuthResult> profileResult;
    std::optional<UserIdentity> identity;
    RestClient client;
    AuthSession session(client, server.Url(QStringLiteral("/api/v1")));

    // El tercer argumento es la lambda que Login guarda como onCompleted. Al pasarla, loginResult sigue vacío.
    session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&loginResult](AuthResult received) {
        // Login llama a onCompleted(std::move(result)). Ese result entra aquí con el nombre received.
        // La captura [&loginResult] permite guardar el resultado en la variable del test que observará WaitFor.
        loginResult = std::move(received);
    });
    // Login ya ha iniciado el POST. WaitFor procesa eventos de Qt hasta que el servidor recibe la petición.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
    // El índice 0 identifica el POST de login. De HttpResponse(200, ...) sale el 200 que llegará en received.statusCode.
    ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(200, ValidLoginResponse)));
    // has_value() pasa a true cuando la lambda guarda received en loginResult, también si hubo un error.
    ASSERT_TRUE(WaitFor([&loginResult] { return loginResult.has_value(); }));
    // Ahora que hay un resultado, comprobamos si el login tuvo éxito.
    ASSERT_EQ(AuthOutcome::Success, loginResult->outcome);

    // El test aporta la callback de FetchUserIdentity. Sus dos variables siguen vivas durante la consulta.
    session.FetchUserIdentity([&profileResult, &identity](AuthResult received, std::optional<UserIdentity> user) {
        profileResult = std::move(received);
        identity = std::move(user);
    });
    // La petición 1 es la consulta del perfil. Se inspecciona antes de enviarle la respuesta.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 2; }));
    const QByteArray& request = server.Requests()[1];
    EXPECT_TRUE(request.startsWith("GET /api/v1/users/me HTTP/1.1\r\n"));
    EXPECT_EQ(request.size() - 4, request.indexOf("\r\n\r\n"));
    // El nombre de la cabecera admite mayúsculas y minúsculas. El valor del token se compara tal como llegó.
    const QByteArray headerName = "\r\nauthorization:";
    const qsizetype headerStart = request.toLower().indexOf(headerName);
    ASSERT_GE(headerStart, 0);
    EXPECT_TRUE(request.sliced(headerStart + headerName.size()).startsWith(" Bearer fake-access\r\n"));

    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(200, R"({"id":7,"companyId":3})")));
    // La callback se ejecuta después de que AuthSession haya interpretado la respuesta del GET.
    ASSERT_TRUE(WaitFor([&profileResult] { return profileResult.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, profileResult->outcome);
    EXPECT_EQ(200, profileResult->statusCode);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(7, identity->id);
    EXPECT_EQ(3, identity->companyId);
}

}  // namespace sbp
