#include "auth_refresh_test.h"

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

// Verifica el correo y la contraseña que recibe el servidor y el éxito que Login entrega al cliente.
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

// Comprueba que la callback recibe el 401 y el código INVALID_CREDENTIALS cuando falla el login.
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

// Comprueba que el login exige tanto un estado 200 como un par de tokens completo.
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

// Verifica que el cliente conserva el 503 aunque el cuerpo carezca de un código de error válido.
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

// Simula un corte antes de las cabeceras o durante el cuerpo y comprueba qué estado HTTP llega al cliente.
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

// Completa un segundo login antes del primero y comprueba que la respuesta antigua se descarta.
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

// Responde al login después de destruir la sesión y comprueba que su callback queda retirada.
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

// Comprueba que el cliente rechaza las URL base inválidas antes de enviar el correo o la contraseña.
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

// Verifica que una llamada sin callback o desde otro hilo permite que el login pendiente termine correctamente.
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

// Comprueba que un 401 en el perfil provoca un refresh y que el segundo GET utiliza el nuevo accessToken.
TEST_F(AuthSessionTest, FetchUserIdentityRetriesWithRefreshedAccessToken) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    std::optional<AuthResult> loginResult;
    std::optional<AuthResult> profileResult;
    std::optional<UserIdentity> identity;
    RestClient client;
    AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));

    // Guarda el resultado del login para iniciar la consulta cuando la sesión tenga el primer par de tokens.
    session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&loginResult](AuthResult received) {
        loginResult = std::move(received);
    });
    // Espera a que el servidor local reciba el POST de login.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
    ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(200, ValidLoginResponse)));
    // El hilo Qt entrega el resultado antes de iniciar el primer GET.
    ASSERT_TRUE(WaitFor([&loginResult] { return loginResult.has_value(); }));
    ASSERT_EQ(AuthOutcome::Success, loginResult->outcome);

    // El receptor del perfil sigue siendo el mismo durante el GET inicial, la renovación y el reintento.
    session.FetchUserIdentity([&profileResult, &identity](AuthResult received, std::optional<UserIdentity> user) {
        profileResult = std::move(received);
        identity = std::move(user);
    });
    // Rechaza el primer GET para que AuthSession tenga que renovar.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 2; }));
    ASSERT_TRUE(server.Requests()[1].startsWith("GET /api/v1/users/me HTTP/1.1\r\n"));
    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(401, R"({"code":"AUTHENTICATION_REQUIRED"})")));

    // Comprueba el POST que desencadenó el 401 antes de entregar los tokens renovados.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 3; }));
    const QByteArray& refresh = server.Requests()[2];
    ASSERT_TRUE(refresh.startsWith("POST /api/v1/auth/refresh HTTP/1.1\r\n"));
    const qsizetype separator = refresh.indexOf("\r\n\r\n");
    ASSERT_GE(separator, 0);
    EXPECT_FALSE(refresh.first(separator).toLower().contains("\r\nauthorization:"));
    const QJsonObject expected{{QStringLiteral("refreshToken"), QStringLiteral("fake-refresh")}};
    EXPECT_EQ(expected, QJsonDocument::fromJson(refresh.mid(separator + 4)).object());
    ASSERT_TRUE(server.ReplyToHeldRequest(2, HttpResponse(200, ValidRefreshResponse)));

    // Inspecciona la cabecera del segundo GET antes de responder con el perfil.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 4; }));
    EXPECT_FALSE(profileResult.has_value());
    const QByteArray& retry = server.Requests()[3];
    ASSERT_TRUE(retry.startsWith("GET /api/v1/users/me HTTP/1.1\r\n"));
    const QByteArray headerName = "\r\nauthorization:";
    const qsizetype headerStart = retry.toLower().indexOf(headerName);
    ASSERT_GE(headerStart, 0);
    EXPECT_TRUE(retry.sliced(headerStart + headerName.size()).startsWith(" Bearer new-access\r\n"));
    ASSERT_TRUE(server.ReplyToHeldRequest(3, HttpResponse(200, R"({"id":7,"companyId":3})")));

    // Espera la entrega del resultado del segundo GET al receptor original.
    ASSERT_TRUE(WaitFor([&profileResult] { return profileResult.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, profileResult->outcome);
    EXPECT_EQ(200, profileResult->statusCode);
    ASSERT_TRUE(identity.has_value());
    EXPECT_EQ(7, identity->id);
    EXPECT_EQ(3, identity->companyId);
}

// Comprueba que un segundo 401 en GET /users/me llega al receptor y termina la consulta tras un solo refresh.
TEST_F(AuthSessionTest, FetchUserIdentityStopsAfterSecond401) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    const QByteArray unauthorized = HttpResponse(401, R"({"code":"AUTHENTICATION_REQUIRED"})");
    std::optional<AuthResult> loginResult;
    std::optional<AuthResult> profileResult;
    int profileCalls = 0;
    RestClient client;
    AuthSession session(client, server.Url(QStringLiteral("/api/v1/")));

    // Guarda el resultado del login antes de pedir el perfil.
    session.Login(QStringLiteral("user@example.test"), QStringLiteral("clave"), [&loginResult](AuthResult received) {
        loginResult = std::move(received);
    });
    // La primera petición permite entregar a la sesión su par inicial de tokens.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 1; }));
    ASSERT_TRUE(server.ReplyToHeldRequest(0, HttpResponse(200, ValidLoginResponse)));
    // Espera a que Login haya aceptado los tokens en el hilo Qt.
    ASSERT_TRUE(WaitFor([&loginResult] { return loginResult.has_value(); }));
    ASSERT_EQ(AuthOutcome::Success, loginResult->outcome);

    // Cuenta las entregas de esta única consulta y comprueba que el error llega sin identidad.
    session.FetchUserIdentity([&profileCalls, &profileResult](AuthResult received, std::optional<UserIdentity> user) {
        ++profileCalls;
        EXPECT_FALSE(user.has_value());
        profileResult = std::move(received);
    });
    // El primer GET recibe el 401 que inicia la renovación.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 2; }));
    ASSERT_TRUE(server.Requests()[1].startsWith("GET /api/v1/users/me HTTP/1.1\r\n"));
    ASSERT_TRUE(server.ReplyToHeldRequest(1, unauthorized));
    // El refresh tiene éxito y entrega al cliente el nuevo par.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 3; }));
    ASSERT_TRUE(server.Requests()[2].startsWith("POST /api/v1/auth/refresh HTTP/1.1\r\n"));
    ASSERT_TRUE(server.ReplyToHeldRequest(2, HttpResponse(200, ValidRefreshResponse)));
    // Rechaza también el GET de reintento. El receptor aún espera su resultado.
    ASSERT_TRUE(WaitFor([&server] { return server.Requests().size() == 4; }));
    ASSERT_TRUE(server.Requests()[3].startsWith("GET /api/v1/users/me HTTP/1.1\r\n"));
    ASSERT_EQ(0, profileCalls);
    ASSERT_TRUE(server.ReplyToHeldRequest(3, unauthorized));

    // Una quinta petición indicaría que el cliente volvió a renovar o reintentar.
    ASSERT_TRUE(WaitFor([&] { return profileResult.has_value() || server.Requests().size() > 4; }));
    ProcessEventsFor(50);
    EXPECT_EQ(4u, server.Requests().size());
    EXPECT_EQ(1, profileCalls);
    ASSERT_TRUE(profileResult.has_value());
    EXPECT_EQ(AuthOutcome::HttpError, profileResult->outcome);
    EXPECT_EQ(401, profileResult->statusCode);
    EXPECT_EQ(QStringLiteral("AUTHENTICATION_REQUIRED"), profileResult->problemCode);
}

// El cierre parte del mismo login y del mismo servidor en modo Hold que las pruebas de renovación.
using AuthLogoutTest = AuthRefreshTest;
constexpr char LogoutConfirmed[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";

// Comprueba que Logout cierra la sesión local antes del 204 y envía al backend el refreshToken del login.
TEST_F(AuthLogoutTest, LogoutClosesLocallyBeforeServerConfirms) {
    std::optional<AuthResult> logoutResult;
    // Guarda la confirmación que Logout entrega al recibir la respuesta del servidor.
    session->Logout([&logoutResult](AuthResult received) {
        logoutResult = std::move(received);
    });

    // Al volver de Logout ya no se puede consultar el perfil, aunque el servidor aún no haya respondido.
    const auto profile = FetchProfile();
    ASSERT_EQ(1, profile->calls);
    EXPECT_EQ(AuthOutcome::NoSession, profile->result.outcome);
    EXPECT_FALSE(profile->identity.has_value());

    // La petición 0 fue el login que hizo SetUp. La 1 debe llevar el secreto en el cuerpo del POST.
    ASSERT_TRUE(WaitForRequest(1, "POST /api/v1/auth/logout"));
    const QByteArray& request = server.Requests()[1];
    const auto separator = request.indexOf("\r\n\r\n");
    ASSERT_GE(separator, 0);
    EXPECT_FALSE(request.first(separator).toLower().contains("\r\nauthorization:"));
    const QJsonObject expected{{QStringLiteral("refreshToken"), QStringLiteral("fake-refresh")}};
    EXPECT_EQ(expected, QJsonDocument::fromJson(request.mid(separator + 4)).object());
    EXPECT_FALSE(logoutResult.has_value());

    ASSERT_TRUE(server.ReplyToHeldRequest(1, LogoutConfirmed));
    // Espera a que el cliente interprete el 204 y entregue el resultado en el hilo Qt.
    ASSERT_TRUE(WaitFor([&logoutResult] { return logoutResult.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, logoutResult->outcome);
    EXPECT_EQ(204, logoutResult->statusCode);
    EXPECT_EQ(2u, server.Requests().size());
}

// Un 503 se entrega al llamador y el cierre local sigue impidiendo consultar el perfil.
TEST_F(AuthLogoutTest, LogoutReports503AndKeepsSessionClosed) {
    std::optional<AuthResult> logoutResult;
    // Recoge la respuesta que el cliente interpreta del POST de cierre.
    session->Logout([&logoutResult](AuthResult received) {
        logoutResult = std::move(received);
    });
    ASSERT_TRUE(WaitForRequest(1, "POST /api/v1/auth/logout"));
    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(503, R"({"code":"SERVICE_UNAVAILABLE"})")));
    // Primero deja que Logout procese el fallo remoto. Después comprueba el estado local.
    ASSERT_TRUE(WaitFor([&logoutResult] { return logoutResult.has_value(); }));
    EXPECT_EQ(AuthOutcome::HttpError, logoutResult->outcome);
    EXPECT_EQ(503, logoutResult->statusCode);
    EXPECT_EQ(QStringLiteral("SERVICE_UNAVAILABLE"), logoutResult->problemCode);
    EXPECT_EQ(RestTransportError::None, logoutResult->transportError);

    const auto profile = FetchProfile();
    ASSERT_EQ(1, profile->calls);
    EXPECT_EQ(AuthOutcome::NoSession, profile->result.outcome);
    EXPECT_FALSE(profile->identity.has_value());
    EXPECT_EQ(2u, server.Requests().size());
}

// Un segundo Logout devuelve NoSession mientras el primero conserva su callback y recibe el único 204.
TEST_F(AuthLogoutTest, SecondLogoutKeepsFirstConfirmation) {
    std::optional<AuthResult> firstResult;
    std::optional<AuthResult> secondResult;
    // La primera llamada debe esperar mientras el servidor retiene su POST.
    session->Logout([&firstResult](AuthResult received) {
        firstResult = std::move(received);
    });
    ASSERT_TRUE(WaitForRequest(1, "POST /api/v1/auth/logout"));
    // La segunda llamada encuentra la sesión cerrada y completa en este mismo hilo.
    session->Logout([&secondResult](AuthResult received) {
        secondResult = std::move(received);
    });
    ASSERT_TRUE(secondResult.has_value());
    EXPECT_EQ(AuthOutcome::NoSession, secondResult->outcome);
    EXPECT_FALSE(secondResult->statusCode.has_value());
    EXPECT_FALSE(firstResult.has_value());

    ASSERT_TRUE(server.ReplyToHeldRequest(1, LogoutConfirmed));
    // El 204 sigue perteneciendo a la primera llamada.
    ASSERT_TRUE(WaitFor([&firstResult] { return firstResult.has_value(); }));
    EXPECT_EQ(AuthOutcome::Success, firstResult->outcome);
    EXPECT_EQ(204, firstResult->statusCode);
    ProcessEventsFor(50);
    EXPECT_EQ(2u, server.Requests().size());
}

// Cerrar durante un Login pendiente descarta su respuesta 200 y los tokens que contiene.
TEST_F(AuthLogoutTest, LogoutDiscardsLateLoginSuccess) {
    const auto loginCalls = std::make_shared<int>(0);
    // Este nuevo Login retira los tokens del login inicial de SetUp y queda pendiente en el servidor.
    session->Login(QStringLiteral("second@example.test"), QStringLiteral("clave"), [loginCalls](AuthResult) {
        ++*loginCalls;
    });
    ASSERT_TRUE(WaitForRequest(1, "POST /api/v1/auth/login"));
    std::optional<AuthResult> logoutResult;
    // Al no disponer todavía de tokens, el cierre debe entregar NoSession inmediatamente.
    session->Logout([&logoutResult](AuthResult received) {
        logoutResult = std::move(received);
    });
    ASSERT_TRUE(logoutResult.has_value());
    EXPECT_EQ(AuthOutcome::NoSession, logoutResult->outcome);

    ASSERT_TRUE(server.ReplyToHeldRequest(1, HttpResponse(200, ValidLoginResponse)));
    // Cuando el test vuelve a ser el único dueño del contador, el cliente ya retiró la callback de Login.
    ASSERT_TRUE(WaitFor([&loginCalls] { return loginCalls.use_count() == 1; }));
    EXPECT_EQ(0, *loginCalls);
    const auto profile = FetchProfile();
    ASSERT_EQ(1, profile->calls);
    EXPECT_EQ(AuthOutcome::NoSession, profile->result.outcome);
    EXPECT_FALSE(profile->identity.has_value());
    EXPECT_EQ(2u, server.Requests().size());
}

// Un refresh que responde después del cierre no repite el GET ni vuelve a abrir la sesión.
TEST_F(AuthLogoutTest, LogoutDiscardsLateRefreshSuccess) {
    const auto pendingProfile = FetchProfile();
    ASSERT_TRUE(ReplyToProfile(1, "fake-access", 401, R"({"code":"AUTHENTICATION_REQUIRED"})"));
    ASSERT_TRUE(WaitForRequest(2, "POST /api/v1/auth/refresh"));
    std::optional<AuthResult> logoutResult;
    // El servidor guarda las dos peticiones para poder confirmar el cierre antes de responder al refresh.
    session->Logout([&logoutResult](AuthResult received) {
        logoutResult = std::move(received);
    });
    ASSERT_TRUE(WaitForRequest(3, "POST /api/v1/auth/logout"));
    ASSERT_TRUE(server.ReplyToHeldRequest(3, LogoutConfirmed));
    // El cierre ya debe estar confirmado cuando llegue el par de tokens de la sesión anterior.
    ASSERT_TRUE(WaitFor([&logoutResult] { return logoutResult.has_value(); }));
    ASSERT_EQ(AuthOutcome::Success, logoutResult->outcome);
    ASSERT_TRUE(ReplyToRefresh(2, HttpResponse(200, ValidRefreshResponse), QStringLiteral("fake-refresh")));
    // Espera a que se libere la callback antigua antes de consultar el perfil otra vez.
    ASSERT_TRUE(WaitFor([&pendingProfile] { return pendingProfile.use_count() == 1; }));
    EXPECT_EQ(0, pendingProfile->calls);

    const auto profile = FetchProfile();
    ASSERT_EQ(1, profile->calls);
    EXPECT_EQ(AuthOutcome::NoSession, profile->result.outcome);
    EXPECT_FALSE(profile->identity.has_value());
    EXPECT_EQ(4u, server.Requests().size());
}

}  // namespace sbp
