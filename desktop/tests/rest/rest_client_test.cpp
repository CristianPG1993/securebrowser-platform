#include "rest_test_support.h"

#include "rest/rest_client.h"

#include <QElapsedTimer>

#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace sbp {

// Patrón de los casos: preparar servidor, enviar con el cliente real, procesar eventos y comprobar RestResult.
// TEST_F utiliza los auxiliares de RestClientTest; ASSERT detiene el caso si falta un requisito y EXPECT comprueba datos.
// El JSON de aquí es una fixture de transporte; estas pruebas no validan DTOs de autenticación ni políticas.

// Comprueba que Send retorna antes de la callback y que la respuesta se entrega una sola vez.
TEST_F(RestClientTest, GetReturnsHttpBodyAsynchronouslyAndOnlyOnce) {
    const QByteArray body = R"({"id":7,"companyId":3})";
    RestTestServer server(HttpResponse(200, body));
    ASSERT_TRUE(server.Listen());
    std::optional<RestResult> result;
    int callbacks = 0;
    RestClient client;
    QNetworkRequest request(server.Url());
    request.setRawHeader("Authorization", "Bearer fake-token");
    // El cliente vive hasta después de comprobar el resultado; la callback conserva referencias locales.
    client.Send(RestClient::Method::Get, request, {}, [&](RestResult received) {
        ++callbacks;
        result = std::move(received);
    });
    EXPECT_FALSE(result.has_value());
    // Procesa la red del servidor y del cliente hasta recibir la respuesta o agotar el watchdog.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
    EXPECT_EQ(200, result->statusCode);
    EXPECT_EQ(body, result->body);
    EXPECT_EQ(RestTransportError::None, result->transportError);
    ASSERT_EQ(1u, server.Requests().size());
    EXPECT_TRUE(server.Requests().front().startsWith("GET /api/v1/devices/17/policy HTTP/1.1\r\n"));
    EXPECT_TRUE(server.Requests().front().toLower().contains("\r\nauthorization: bearer fake-token\r\n"));
    ProcessEventsFor(50);
    EXPECT_EQ(1, callbacks);
}

// Comprueba los bytes del POST y las cabeceras recibidas por el servidor, sin autenticar contra un backend.
TEST_F(RestClientTest, PostPreservesJsonBodyWithoutImplicitAuthorization) {
    const QByteArray body = R"({"email":"fixture@example.test","password":"fake-only"})";
    RestTestServer server(HttpResponse(200, R"({"accessToken":"fake-token"})"));
    ASSERT_TRUE(server.Listen());
    std::optional<RestResult> result;
    RestClient client;
    QNetworkRequest request(server.Url(QStringLiteral("/api/v1/auth/login")));
    // Los valores son fixtures inertes; no se utilizan secretos del entorno ni del backend.
    client.Send(RestClient::Method::Post, request, body, [&](RestResult received) {
        result = std::move(received);
    });
    // Mantiene vivos request, result y client mientras se procesa la operación asíncrona.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
    EXPECT_EQ(200, result->statusCode);
    EXPECT_EQ(RestTransportError::None, result->transportError);
    ASSERT_EQ(1u, server.Requests().size());
    const QByteArray& sent = server.Requests().front();
    EXPECT_TRUE(sent.startsWith("POST /api/v1/auth/login HTTP/1.1\r\n"));
    EXPECT_TRUE(sent.toLower().contains("\r\ncontent-type: application/json\r\n"));
    EXPECT_FALSE(sent.toLower().contains("\r\nauthorization:"));
    EXPECT_TRUE(sent.endsWith("\r\n\r\n" + body));
}

// Una operación rechazada por el servidor conserva el código HTTP y no se confunde con perder la conexión.
TEST_F(RestClientTest, Http401And503PreserveStatusAndAreNotTransportFailures) {
    for (int status : {401, 503}) {
        SCOPED_TRACE(status);
        const QByteArray body = R"({"code":"FIXTURE_ERROR"})";
        RestTestServer server(HttpResponse(status, body));
        ASSERT_TRUE(server.Listen());
        std::optional<RestResult> result;
        RestClient client;
        // Cada iteración posee su cliente y resultado; no comparten estado de otra respuesta.
        client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult received) {
            result = std::move(received);
        });
        // La espera termina antes de retirar las referencias que conserva la callback.
        ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
        EXPECT_EQ(status, result->statusCode);
        EXPECT_EQ(body, result->body);
        EXPECT_EQ(RestTransportError::None, result->transportError);
    }
}

// Simula un corte anterior a cualquier respuesta HTTP, usando el modo Disconnect del servidor.
TEST_F(RestClientTest, ServerDisconnectIsTransportFailureWithoutHttpStatus) {
    RestTestServer server({}, RestTestServer::Mode::Disconnect);
    ASSERT_TRUE(server.Listen());
    std::optional<RestResult> result;
    RestClient client;
    // El servidor acepta la petición y cierra el socket sin enviar una línea de estado HTTP.
    client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult received) {
        result = std::move(received);
    });
    // Incluye los posibles reintentos de conexión internos de Qt, sin temporizadores de producción modificados.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
    EXPECT_FALSE(result->statusCode.has_value());
    EXPECT_EQ(RestTransportError::Network, result->transportError);
}

// Recibir 200 no basta: si el cuerpo no llega completo, todavía existe un fallo de transporte.
TEST_F(RestClientTest, TruncatedHttp200IsNotSuccessfulTransport) {
    RestTestServer server("HTTP/1.1 200 OK\r\nContent-Length: 100\r\nConnection: close\r\n\r\n{}");
    ASSERT_TRUE(server.Listen());
    std::optional<RestResult> result;
    RestClient client;
    // Un 200 seguido de un cuerpo incompleto debe conservar el fallo de transporte.
    client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult received) {
        result = std::move(received);
    });
    // El servidor cierra antes de entregar los bytes declarados; no se consulta un backend externo.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
    EXPECT_EQ(200, result->statusCode);
    EXPECT_EQ(RestTransportError::Network, result->transportError);
}

// Hold deja la petición abierta para que venza el temporizador real del cliente.
TEST_F(RestClientTest, PendingRequestTimesOutAfterTheProductionDeadline) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    std::optional<RestResult> result;
    int callbacks = 0;
    RestClient client;
    QElapsedTimer elapsed;
    elapsed.start();
    // No responde el servidor: se ejercita el plazo total real de 15 segundos del cliente.
    client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult received) {
        ++callbacks;
        result = std::move(received);
    });
    // El watchdog deja margen al planificador; el límite independiente de CTest también acota esta prueba.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }, 20000));
    EXPECT_GE(elapsed.elapsed(), 15000);
    EXPECT_EQ(RestTransportError::Timeout, result->transportError);
    EXPECT_FALSE(result->statusCode.has_value());
    ASSERT_EQ(1u, server.Requests().size());
    ProcessEventsFor(50);
    EXPECT_EQ(1, callbacks);
}

// Dos operaciones concurrentes permiten detectar una cancelación omitida o una entrega duplicada.
TEST_F(RestClientTest, CancelAllDeliversOneCancellationPerPendingRequest) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    int callbacks = 0;
    RestClient client;
    for (int index = 0; index < 2; ++index) {
        // Ambos intentos conservan su propia operación; cada callback comprueba su resultado de cancelación.
        client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult result) {
            EXPECT_EQ(RestTransportError::Cancelled, result.transportError);
            ++callbacks;
        });
    }
    // Espera a que ambas peticiones estén activas en el servidor antes de cancelarlas.
    ASSERT_TRUE(WaitFor([&] { return server.Requests().size() == 2; }));
    client.CancelAll();
    // Cada petición debe completar exactamente una vez, sin esperar al timeout de producción.
    ASSERT_TRUE(WaitFor([&] { return callbacks == 2; }));
    client.CancelAll();
    ProcessEventsFor(50);
    EXPECT_EQ(2, callbacks);
}

// Al destruir el cliente se retiran las entregas pendientes, en lugar de llamar a receptores que podrían haber terminado.
TEST_F(RestClientTest, DestructionWithdrawsPendingCallbacks) {
    RestTestServer server({}, RestTestServer::Mode::Hold);
    ASSERT_TRUE(server.Listen());
    int callbacks = 0;
    auto client = std::make_unique<RestClient>();
    // La callback solo cuenta entregas: destruir el cliente debe retirar las que siguen pendientes.
    client->Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult) {
        ++callbacks;
    });
    // Confirma que había una petición real en curso antes de destruir el cliente.
    ASSERT_TRUE(WaitFor([&] { return server.Requests().size() == 1; }));
    client.reset();
    ProcessEventsFor(50);
    EXPECT_EQ(0, callbacks);
}

// Reproduce un receptor que decide cerrar su cliente al recibir la respuesta.
TEST_F(RestClientTest, CompletionCallbackMayDestroyTheClient) {
    RestTestServer server(HttpResponse(200, "{}"));
    ASSERT_TRUE(server.Listen());
    int callbacks = 0;
    auto client = std::make_unique<RestClient>();
    // La entrega diferida debe permitir destruir el gestor fuera de la señal finished de la respuesta.
    client->Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, [&](RestResult result) {
        EXPECT_EQ(RestTransportError::None, result.transportError);
        ++callbacks;
        client.reset();
    });
    // La variable client permanece viva aunque la callback retire el objeto al que apuntaba.
    ASSERT_TRUE(WaitFor([&] { return callbacks == 1; }));
    EXPECT_FALSE(client);
    ProcessEventsFor(50);
    EXPECT_EQ(1, callbacks);
}

// Los argumentos incorrectos lanzan una excepción local; no deben provocar tráfico ni una callback posterior.
TEST_F(RestClientTest, RejectsInvalidRequestsBeforeSendingAnything) {
    RestTestServer server(HttpResponse(200, "{}"));
    ASSERT_TRUE(server.Listen());
    int callbacks = 0;
    RestClient client;
    // Todos los rechazos deben ser síncronos y anteriores a cualquier envío de red.
    const auto completed = [&](RestResult) { ++callbacks; };
    EXPECT_THROW(client.Send(RestClient::Method::Get,
        QNetworkRequest(QUrl(QStringLiteral("http://backend.invalid/api/v1/devices"))), {}, completed),
        std::invalid_argument);
    EXPECT_THROW(client.Send(RestClient::Method::Get,
        QNetworkRequest(QUrl(QStringLiteral("https://user:password@example.test/api/v1/devices"))), {}, completed),
        std::invalid_argument);
    EXPECT_THROW(client.Send(RestClient::Method::Get,
        QNetworkRequest(QUrl(QStringLiteral("https://example.test/api/v1/devices#fragment"))), {}, completed),
        std::invalid_argument);
    EXPECT_THROW(client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), "{}", completed),
        std::invalid_argument);
    EXPECT_THROW(client.Send(static_cast<RestClient::Method>(99), QNetworkRequest(server.Url()), {}, completed),
        std::invalid_argument);
    EXPECT_THROW(client.Send(RestClient::Method::Get, QNetworkRequest(server.Url()), {}, {}),
        std::invalid_argument);
    ProcessEventsFor(50);
    EXPECT_TRUE(server.Requests().empty());
    EXPECT_EQ(0, callbacks);
}

// Dos servidores permiten comprobar que un Location recibido no provoca una segunda conexión al destino.
TEST_F(RestClientTest, ReturnsRedirectWithoutFollowingItOrForwardingAuthorization) {
    RestTestServer destination(HttpResponse(200, "{}"));
    ASSERT_TRUE(destination.Listen());
    const QByteArray redirect = "HTTP/1.1 302 Found\r\nLocation: " + destination.Url().toEncoded()
        + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    RestTestServer origin(redirect);
    ASSERT_TRUE(origin.Listen());
    std::optional<RestResult> result;
    RestClient client;
    QNetworkRequest request(origin.Url());
    request.setRawHeader("Authorization", "Bearer fake-token");
    // La autorización de fixture solo puede llegar al origen solicitado, nunca al destino de Location.
    client.Send(RestClient::Method::Get, request, {}, [&](RestResult received) {
        result = std::move(received);
    });
    // Espera la respuesta original; después procesa eventos para detectar seguimientos indebidos.
    ASSERT_TRUE(WaitFor([&] { return result.has_value(); }));
    EXPECT_EQ(302, result->statusCode);
    EXPECT_EQ(RestTransportError::None, result->transportError);
    ProcessEventsFor(50);
    EXPECT_TRUE(destination.Requests().empty());
}

}  // namespace sbp
