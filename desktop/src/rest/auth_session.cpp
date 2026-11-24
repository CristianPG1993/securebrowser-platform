#include "rest/auth_session.h"
#include "rest/token_response_parser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sbp {

// Prepara las dependencias y el estado local; la comunicación HTTP comenzará al llamar a Login.
AuthSession::AuthSession(RestClient& client, QUrl apiBaseUrl)
    : client_(client),
      // La URL recibida por valor pasa al miembro y conserva sus datos durante la vida de la sesión.
      apiBaseUrl_(std::move(apiBaseUrl)),
      // Construye State con generation=0 y tokens vacío; las futuras callbacks usarán una referencia débil.
      state_(std::make_shared<State>()),
      // Solo conserva la identidad del hilo actual; no crea ni arranca otro hilo.
      ownerThread_(QThread::currentThread()) {}

void AuthSession::Login(const QString& email, const QString& password, CompletionCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("Auth login requires a completion callback.");
    }

    // Valida la configuración antes de cambiar el estado o construir un cuerpo con credenciales.
    // La base identifica la API; query, fragmento y credenciales dentro de la URL no forman parte de ella.
    QUrl base(apiBaseUrl_);
    const QString host = base.host();
    const bool loopback = host == QStringLiteral("localhost")
        || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
    if (!base.isValid() || base.isRelative() || host.isEmpty() || base.hasQuery() || base.hasFragment()
        || base.authority().contains(QLatin1Char('@'))
        || (base.scheme() != QStringLiteral("https") && !(base.scheme() == QStringLiteral("http") && loopback))) {
        throw std::invalid_argument("Auth requires an HTTPS API base or demo loopback HTTP, without credentials, query or fragment.");
    }
    // La barra final conserva /api/v1 al resolver la ruta relativa auth/login.
    if (!base.path().endsWith(QLatin1Char('/'))) {
        base.setPath(base.path() + QLatin1Char('/'));
    }
    QNetworkRequest request(base.resolved(QUrl(QStringLiteral("auth/login"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    // Qt escapa el contenido del email y la contraseña como JSON. No se recorta ni normaliza la contraseña.
    const QJsonObject credentials{{QStringLiteral("email"), email}, {QStringLiteral("password"), password}};
    const QByteArray body = QJsonDocument(credentials).toJson(QJsonDocument::Compact);
    if (state_->generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Auth session generations exhausted.");
    }
    // Cada intento retira el par anterior y recibe una generación no reutilizada.
    const std::uint64_t attempt = ++state_->generation;
    state_->tokens.reset();
    const std::weak_ptr<State> weakState(state_);

    // El transporte ejecuta esta lambda en Qt. No captura this ni las referencias email/password.
    // weakState evita acceder a una sesión destruida; attempt descarta tanto éxitos como errores obsoletos.
    client_.Send(RestClient::Method::Post, request, body,
        [weakState, attempt, onCompleted = std::move(onCompleted)](RestResult received) mutable {
            const auto state = weakState.lock();
            if (!state || state->generation != attempt) {
                return;
            }

            AuthResult result;
            result.statusCode = received.statusCode;
            if (received.transportError != RestTransportError::None) {
                // Un fallo de transporte tiene prioridad aunque haya llegado un estado HTTP parcial.
                result.outcome = AuthOutcome::TransportError;
                result.transportError = received.transportError;
            } else if (received.statusCode == 200) {
                auto tokens = TokenResponseParser::Parse(received.body);
                if (tokens.has_value()) {
                    // Publica ambos tokens juntos y solo para el intento actual; el receptor no recibe los secretos.
                    state->tokens = std::move(*tokens);
                    result.outcome = AuthOutcome::Success;
                }
                // Un HTTP 200 con DTO inválido conserva el resultado predeterminado InvalidResponse.
            } else if (received.statusCode.has_value() && *received.statusCode >= 300) {
                result.outcome = AuthOutcome::HttpError;
                // Un error HTTP conserva su estado aunque el cuerpo no sea un Problem Details válido.
                const QJsonDocument problem = QJsonDocument::fromJson(received.body);
                if (problem.isObject()) {
                    const QJsonValue code = problem.object().value(QStringLiteral("code"));
                    if (code.isString() && !code.toString().trimmed().isEmpty()) {
                        result.problemCode = code.toString();
                    }
                }
            }

            // La callback y el resultado son propios de esta llamada. El transporte contiene sus excepciones.
            // Después de invocarla no se utiliza AuthSession: el receptor puede haber decidido destruirla.
            onCompleted(std::move(result));
        });
}

void AuthSession::RequireOwnerThread() const {
    // Rechaza la llamada antes de tocar tokens o generaciones, igual que el transporte protege su gestor Qt.
    if (QThread::currentThread() != ownerThread_) {
        throw std::logic_error("Auth session must be used from its owning Qt thread.");
    }
}

}  // namespace sbp
