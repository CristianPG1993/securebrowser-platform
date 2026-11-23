#pragma once

#include <QByteArray>
#include <QTcpServer>
#include <QUrl>

#include <gtest/gtest.h>

#include <functional>
#include <map>
#include <vector>

class QTcpSocket;

namespace sbp {

// Servidor HTTP mínimo de pruebas: un único intercambio por conexión, solo en loopback.
// La conexión TCP local es real; este objeto simula las respuestas del backend sin Spring Boot ni PostgreSQL.
class RestTestServer final {
public:
    // Reply envía la respuesta preparada; Hold espera sin responder; Disconnect corta sin respuesta HTTP.
    enum class Mode { Reply, Hold, Disconnect };

    // response contiene el mensaje HTTP completo que el caso quiere recibir: estado, cabeceras y cuerpo.
    explicit RestTestServer(QByteArray response, Mode mode = Mode::Reply);
    ~RestTestServer();

    // Abre la escucha en 127.0.0.1 con puerto libre; devuelve false si Qt no pudo abrirla.
    [[nodiscard]] bool Listen();
    // Construye la URL con el puerto asignado por Listen, para que RestClient sepa a dónde conectarse.
    [[nodiscard]] QUrl Url(const QString& path = QStringLiteral("/api/v1/devices/17/policy")) const;
    // Conserva los mensajes completos recibidos para comprobar método, cabeceras y cuerpo enviados.
    [[nodiscard]] const std::vector<QByteArray>& Requests() const;

private:
    // Atiende el aviso newConnection de Qt y conecta readyRead para cada socket aceptado.
    void AcceptConnections();
    // Acumula los fragmentos TCP hasta completar una petición y aplica el modo de respuesta elegido.
    void ReadRequest(QTcpSocket* socket);

    // QTcpServer es una clase de Qt: abre el puerto y posee los sockets de las conexiones aceptadas.
    QTcpServer server_;
    QByteArray response_;
    Mode mode_;
    // TCP puede entregar un mensaje por partes; cada socket necesita su propio acumulador de bytes.
    std::map<QTcpSocket*, QByteArray> buffers_;
    // Las pruebas usan exclusivamente datos ficticios, incluidos los posibles tokens de estos mensajes.
    std::vector<QByteArray> requests_;
};

// Utiliza eventos Qt acotados por un watchdog; no duerme el hilo que atiende la red.
class RestClientTest : public ::testing::Test {
protected:
    // Procesa eventos hasta cumplir la condición o vencer el watchdog; false indica que la espera no se cumplió.
    [[nodiscard]] static bool WaitFor(const std::function<bool()>& condition, int timeoutMilliseconds = 3000);
    // Deja procesar eventos pendientes, por ejemplo para detectar una segunda callback que no debería llegar.
    static void ProcessEventsFor(int milliseconds);
    // Construye el mensaje HTTP completo a partir de un estado y un cuerpo de fixture.
    [[nodiscard]] static QByteArray HttpResponse(int status, const QByteArray& body);
};

}  // namespace sbp
