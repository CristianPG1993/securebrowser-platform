#include "rest_test_support.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QTcpSocket>
#include <QTimer>

#include <utility>

namespace sbp {

RestTestServer::RestTestServer(QByteArray response, Mode mode)
    : response_(std::move(response)), mode_(mode) {
    // Las conexiones de las pruebas permanecen en el equipo y no deben depender de un proxy externo.
    server_.setProxy(QNetworkProxy::NoProxy);
    // Se ejecuta en el hilo Qt de las pruebas; server_ retira la conexión al destruirse.
    QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] {
        AcceptConnections();
    });
}

RestTestServer::~RestTestServer() {
    // Retira receptores antes de destruir los buffers y los sockets que posee server_.
    for (const auto& entry : buffers_) {
        QObject::disconnect(entry.first, nullptr, &server_, nullptr);
    }
}

bool RestTestServer::Listen() {
    // listen abre el socket de escucha; LocalHost limita IPv4 a 127.0.0.1 y 0 pide un puerto libre a Windows.
    // El HTTP se interpreta en ReadRequest; QTcpServer se ocupa de aceptar la conexión TCP.
    return server_.listen(QHostAddress::LocalHost, 0);
}

QUrl RestTestServer::Url(const QString& path) const {
    // El endpoint de prueba usa el puerto real de esta ejecución, sin ocupar el puerto del backend demo.
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(QStringLiteral("127.0.0.1"));
    url.setPort(server_.serverPort());
    url.setPath(path);
    return url;
}

const std::vector<QByteArray>& RestTestServer::Requests() const {
    return requests_;
}

bool RestTestServer::ReplyToHeldRequest(std::size_t index, const QByteArray& response) {
    if (index >= heldSockets_.size() || !heldSockets_[index]) {
        return false;
    }
    // Cada índice identifica una conexión retenida y solo se puede responder una vez.
    QTcpSocket* socket = heldSockets_[index].data();
    heldSockets_[index].clear();
    const auto written = socket->write(response);
    socket->disconnectFromHost();
    return written == response.size();
}

void RestTestServer::AcceptConnections() {
    while (server_.hasPendingConnections()) {
        // Un socket representa el canal de una conexión aceptada, no la petición HTTP ya interpretada.
        QTcpSocket* socket = server_.nextPendingConnection();
        buffers_.emplace(socket, QByteArray{});
        // Qt mantiene el socket como hijo del servidor; el receptor solo lo usa durante esta prueba.
        QObject::connect(socket, &QTcpSocket::readyRead, &server_, [this, socket] {
            ReadRequest(socket);
        });
        // El peer puede haber enviado los bytes antes de que se conectara readyRead.
        ReadRequest(socket);
    }
}

void RestTestServer::ReadRequest(QTcpSocket* socket) {
    auto& buffer = buffers_.at(socket);
    // readAll recoge los bytes disponibles ahora; pueden faltar cabeceras o parte del cuerpo del POST.
    buffer += socket->readAll();
    // En HTTP/1.1 la línea vacía CRLF CRLF separa las cabeceras del cuerpo.
    const qsizetype separator = buffer.indexOf("\r\n\r\n");
    if (separator < 0) {
        return;
    }

    qsizetype contentLength = 0;
    // El cliente de estos casos envía cuerpos QByteArray con Content-Length; el servidor no implementa chunked.
    const auto lines = buffer.left(separator).split('\n');
    for (const auto& line : lines) {
        if (line.toLower().startsWith("content-length:")) {
            bool valid = false;
            contentLength = line.mid(line.indexOf(':') + 1).trimmed().toLongLong(&valid);
            if (!valid || contentLength < 0) {
                socket->abort();
                return;
            }
        }
    }
    const qsizetype requestLength = separator + 4 + contentLength;
    if (buffer.size() < requestLength) {
        // Espera otro readyRead; responder aquí podría ocultar que un POST no llegó completo.
        return;
    }

    // Conserva una copia completa para que el test pueda afirmar qué recibió realmente el servidor.
    requests_.push_back(buffer.left(requestLength));
    // Se atiende una sola petición por socket; Connection: close hace explícito ese contrato.
    QObject::disconnect(socket, nullptr, &server_, nullptr);
    if (mode_ == Mode::Disconnect) {
        socket->abort();
    } else if (mode_ == Mode::Reply) {
        socket->write(response_);
        // Qt cierra después de enviar lo pendiente; si la fixture declara más bytes, el cliente detectará truncamiento.
        socket->disconnectFromHost();
    } else {
        // La prueba decide cuándo responder, incluso si otro login más reciente ya terminó.
        heldSockets_.push_back(socket);
    }
}

bool RestClientTest::WaitFor(const std::function<bool()>& condition, int timeoutMilliseconds) {
    if (condition()) {
        return true;
    }
    QEventLoop loop;
    // poll revisa la condición; watchdog evita que una prueba se quede esperando indefinidamente.
    // Este watchdog pertenece al test y es distinto del timeout de 15 segundos de RestClient.
    QTimer poll;
    QTimer watchdog;
    watchdog.setSingleShot(true);
    // La condición y el bucle viven hasta salir de exec; todas las funciones trabajan en este hilo Qt.
    QObject::connect(&poll, &QTimer::timeout, &loop, [&condition, &loop] {
        if (condition()) {
            loop.quit();
        }
    });
    QObject::connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start(5);
    watchdog.start(timeoutMilliseconds);
    // El bucle atiende sockets, señales y temporizadores: tanto el servidor como el cliente necesitan esos eventos.
    loop.exec();
    return condition();
}

void RestClientTest::ProcessEventsFor(int milliseconds) {
    // A diferencia de sleep, este bucle deja que lleguen los avisos Qt cuya ausencia queremos comprobar.
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QByteArray RestClientTest::HttpResponse(int status, const QByteArray& body) {
    // Content-Length cuenta bytes del cuerpo; Connection: close permite una respuesta por conexión.
    return "HTTP/1.1 " + QByteArray::number(status) + " Test\r\n"
        "Content-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size())
        + "\r\nConnection: close\r\n\r\n" + body;
}

}  // namespace sbp
