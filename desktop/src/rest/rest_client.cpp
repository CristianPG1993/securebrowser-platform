#include "rest/rest_client.h"

#include <QList>
#include <QMetaMethod>
#include <QNetworkReply>
#include <QPointer>
#include <QRestReply>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QThread>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sbp {

RestClient::~RestClient() {
    // El gestor todavía vive. Desconectar primero impide entregar callbacks durante su destrucción.
    const auto replies = OwnedReplies();
    for (auto* reply : replies) {
        QObject::disconnect(reply, nullptr, &networkManager_, nullptr);
    }
}

void RestClient::Send(Method method, const QNetworkRequest& request,
                      const QByteArray& body, CompletionCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("REST request requires a completion callback.");
    }

    const QNetworkRequest prepared = PrepareRequest(method, request, body);
    QNetworkReply* reply = method == Method::Get
        ? networkManager_.get(prepared) : networkManager_.post(prepared, body);

    // El temporizador pertenece a la respuesta; ambos viven y se utilizan solo en este hilo Qt.
    auto* timer = new QTimer(reply);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    // Las dos funciones conservan el motivo del aborto y una única entrega consumible por petición.
    const auto timedOut = std::make_shared<bool>(false);
    const auto completion = std::make_shared<CompletionCallback>(std::move(onCompleted));

    // Consulta la señal en ejecución para evitar importar QTimer::staticMetaObject como dato.
    const QMetaObject* timerMeta = timer->metaObject();
    const int timeoutIndex = timerMeta->indexOfSignal("timeout()");
    // Se ejecuta en Qt: fijar primero el motivo distingue el timeout de una cancelación explícita.
    const auto timeoutConnection = QMetaObject::connect(
        timer, timerMeta->method(timeoutIndex), reply, [reply, timedOut] {
            *timedOut = true;
            reply->abort();
        });

    // Se ejecuta en Qt y no captura this. Consume la callback para preparar una sola entrega.
    // deliveryContext pertenece al cliente y retira la entrega encolada si este se destruye.
    const auto finish = [reply, timer, timedOut, completion, deliveryContext = &networkManager_] {
        if (!*completion) {
            return;
        }
        auto callback = std::exchange(*completion, CompletionCallback{});
        timer->stop();
        RestResult result = ReadResult(*reply, *timedOut);
        // Qt exige retirada diferida de la respuesta desde finished; no se utiliza después de la entrega.
        reply->deleteLater();
        // Entregar fuera de la señal finished permite que el receptor destruya el cliente sin destruir
        // la respuesta dentro de su propio callback de red. La tarea conserva únicamente datos propios.
        QTimer::singleShot(0, deliveryContext,
            [callback = std::move(callback), result = std::move(result)]() mutable {
                DeliverResult(std::move(callback), std::move(result));
            });
    };
    // La conexión tipada importa staticMetaObject como dato y MSVC no puede diferir esa importación.
    // Consultar los metadatos del objeto conserva /DELAYLOAD, igual que en la interfaz Qt existente.
    const QMetaObject* replyMeta = reply->metaObject();
    const int finishedIndex = replyMeta->indexOfSignal("finished()");
    const auto connection = QMetaObject::connect(
        reply, replyMeta->method(finishedIndex), &networkManager_, finish);
    if (!timeoutConnection || !connection) {
        // Si solo falló el temporizador, retirar finished antes de abortar evita una segunda entrega.
        if (connection) {
            QObject::disconnect(connection);
        }
        reply->abort();
        reply->deleteLater();
        std::fputs("REST response signals could not be connected.\n", stderr);
        // El gestor retira esta entrega si se destruye; el fallo de conexión de señal no es una cancelación del usuario.
        QTimer::singleShot(0, &networkManager_, [completion] {
            auto callback = std::exchange(*completion, CompletionCallback{});
            RestResult result;
            result.transportError = RestTransportError::Network;
            DeliverResult(std::move(callback), std::move(result));
        });
        return;
    }

    if (reply->isFinished()) {
        // Algunos errores pueden estar resueltos al crear la respuesta; mantener también aquí la entrega asíncrona.
        // El gestor actúa como contexto y retira esta tarea si el cliente se destruye antes de atenderla.
        QTimer::singleShot(0, &networkManager_, finish);
    } else {
        timer->start(RequestTimeoutMilliseconds);
    }
}

void RestClient::CancelAll() {
    RequireOwnerThread();
    // Las referencias débiles evitan conservar respuestas destruidas durante la cancelación.
    QList<QPointer<QNetworkReply>> replies;
    const auto children = OwnedReplies();
    for (auto* reply : children) {
        replies.append(QPointer<QNetworkReply>(reply));
    }
    // La lista es local: después de abortar no se vuelve a consultar ningún miembro de este cliente.
    for (const auto& reply : replies) {
        if (reply && !reply->isFinished()) {
            reply->abort();
        }
    }
}

QList<QNetworkReply*> RestClient::OwnedReplies() const {
    QList<QNetworkReply*> replies;
    for (QObject* child : networkManager_.children()) {
        // inherits consulta el tipo en ejecución; findChildren<T> y qobject_cast<T> importarían el dato estático.
        if (child->inherits("QNetworkReply")) {
            replies.append(static_cast<QNetworkReply*>(child));
        }
    }
    return replies;
}

void RestClient::RequireOwnerThread() const {
    if (QThread::currentThread() != networkManager_.thread()) {
        throw std::logic_error("REST client must be used from its owning Qt thread.");
    }
}

QNetworkRequest RestClient::PrepareRequest(
    Method method, const QNetworkRequest& request, const QByteArray& body) {
    if (method != Method::Get && method != Method::Post) {
        throw std::invalid_argument("Unsupported REST method.");
    }
    if (method == Method::Get && !body.isEmpty()) {
        throw std::invalid_argument("REST GET requests must not contain a body.");
    }

    const QUrl url = request.url();
    const QString scheme = url.scheme();
    const QString host = url.host();
    const bool localDemo = host == QStringLiteral("localhost")
        || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
    if (!url.isValid() || url.isRelative() || host.isEmpty()
        || url.authority().contains(QLatin1Char('@')) || url.hasFragment()
        || (scheme != QStringLiteral("https") && !(scheme == QStringLiteral("http") && localDemo))) {
        // Mensaje controlado: la URL puede contener datos que no deben terminar en logs.
        throw std::invalid_argument("REST requires HTTPS or demo loopback HTTP, without URL credentials or fragment.");
    }

    QNetworkRequest prepared(request);
    prepared.setRawHeader("Accept", "application/json, application/problem+json");
    if (method == Method::Post && !prepared.hasRawHeader("Content-Type")) {
        prepared.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    // El token se enviará solo al destino solicitado; el cliente no sigue redirecciones de la API.
    prepared.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    prepared.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    prepared.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);
    // La sesión REST utilizará Bearer explícito, sin conservar cookies ni reutilizar autenticación implícita.
    prepared.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    prepared.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    prepared.setAttribute(QNetworkRequest::AuthenticationReuseAttribute, QNetworkRequest::Manual);
    prepared.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, false);
    // Se utiliza solo el plazo total propio, sin otro timeout de inactividad que compita con su clasificación.
    prepared.setTransferTimeout(0);
    if (scheme == QStringLiteral("https")) {
        auto ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
        prepared.setSslConfiguration(ssl);
    }
    return prepared;
}

RestResult RestClient::ReadResult(QNetworkReply& reply, bool timedOut) {
    // Esta utilidad del mismo módulo Qt distingue errores HTTP de fallos reales de red/protocolo.
    QRestReply response(&reply);
    RestResult result;
    const int status = response.httpStatus();
    if (status >= 100 && status <= 599) {
        result.statusCode = status;
    }
    // Un aborto puede cerrar el dispositivo; conservar solo los bytes que todavía se puedan leer.
    if (reply.isReadable()) {
        result.body = response.readBody();
    }

    if (timedOut) {
        result.transportError = RestTransportError::Timeout;
    } else {
        switch (response.error()) {
        case QNetworkReply::NoError:
            // Sin estado HTTP no existe una respuesta REST válida, aunque Qt no señale otro error.
            result.transportError = result.statusCode.has_value()
                ? RestTransportError::None : RestTransportError::Network;
            break;
        case QNetworkReply::TimeoutError:
        case QNetworkReply::ProxyTimeoutError:
            result.transportError = RestTransportError::Timeout;
            break;
        case QNetworkReply::OperationCanceledError:
            result.transportError = RestTransportError::Cancelled;
            break;
        default:
            result.transportError = RestTransportError::Network;
            break;
        }
    }
    return result;
}

void RestClient::DeliverResult(CompletionCallback onCompleted, RestResult result) noexcept {
    try {
        onCompleted(std::move(result));
    } catch (...) {
        // No registrar la excepción: podría incluir credenciales o el cuerpo de la respuesta.
        std::fputs("REST completion callback failed.\n", stderr);
    }
}

}  // namespace sbp
