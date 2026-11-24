#include "rest/auth_session.h"
#include "rest/token_response_parser.h"
#include "rest/user_identity_parser.h"

#include <QHttpHeaders>
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

    QNetworkRequest request(EndpointUrl(QStringLiteral("auth/login")));
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

    // RestClient llamará a esta lambda en el hilo Qt y le pasará la respuesta HTTP en received.
    // La captura onCompleted guarda la función que el llamador pasó como tercer argumento a Login.
    // weakState evita acceder a una sesión destruida; attempt descarta tanto éxitos como errores obsoletos.
    client_.Send(RestClient::Method::Post, request, body,
        [weakState, attempt, onCompleted = std::move(onCompleted)](RestResult received) mutable {
            auto state = weakState.lock();
            if (!state || state->generation != attempt) {
                return;
            }

            AuthResult result = ReadResponseStatus(received);
            if (result.transportError == RestTransportError::None && result.statusCode == 200) {
                auto tokens = TokenResponseParser::Parse(received.body);
                if (tokens.has_value()) {
                    // Publica ambos tokens juntos y solo para el intento actual; el receptor no recibe los secretos.
                    state->tokens = std::move(*tokens);
                    result.outcome = AuthOutcome::Success;
                }
                // Un HTTP 200 con DTO inválido conserva el resultado predeterminado InvalidResponse.
            }

            // El receptor puede destruir la sesión y procesar más eventos Qt dentro de su callback.
            // Liberar nuestra referencia permite que las demás peticiones detecten esa destrucción.
            state.reset();
            // Aquí se ejecuta la función del llamador. Su parámetro AuthResult recibe el result que acabamos de preparar.
            onCompleted(std::move(result));
        });
}

void AuthSession::FetchUserIdentity(IdentityCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("User identity request requires a completion callback.");
    }
    if (!state_->tokens) {
        AuthResult result;
        result.outcome = AuthOutcome::NoSession;
        onCompleted(std::move(result), std::nullopt);
        return;
    }

    QNetworkRequest request(EndpointUrl(QStringLiteral("users/me")));
    QHttpHeaders headers;
    const QByteArray bearer = "Bearer " + state_->tokens->accessToken.toUtf8();
    if (!headers.append(QHttpHeaders::WellKnownHeader::Authorization, bearer)) {
        // Si Qt rechaza el valor de la cabecera, la petición termina aquí con InvalidResponse.
        onCompleted(AuthResult{}, std::nullopt);
        return;
    }
    request.setHeaders(std::move(headers));

    // Consultar el perfil pertenece a la generación que abrió Login y conserva ese número durante el GET.
    const auto generation = state_->generation;
    const std::weak_ptr<State> weakState(state_);
    // RestClient ejecuta la lambda en el hilo Qt y le pasa la respuesta de este GET. El cuerpo enviado está vacío.
    client_.Send(RestClient::Method::Get, request, {},
        [weakState, generation, onCompleted = std::move(onCompleted)](RestResult received) {
            if (const auto state = weakState.lock(); !state || state->generation != generation) {
                return;
            }

            AuthResult result = ReadResponseStatus(received);
            std::optional<UserIdentity> identity;
            if (result.transportError == RestTransportError::None && result.statusCode == 200) {
                identity = UserIdentityParser::Parse(received.body);
                if (identity) {
                    result.outcome = AuthOutcome::Success;
                }
            }
            // El receptor recibe los dos argumentos juntos. Solo Success lleva una identidad.
            onCompleted(std::move(result), std::move(identity));
        });
}

QUrl AuthSession::EndpointUrl(const QString& relativePath) const {
    QUrl base(apiBaseUrl_);
    const QString host = base.host();
    const bool loopback = host == QStringLiteral("localhost")
        || host == QStringLiteral("127.0.0.1") || host == QStringLiteral("::1");
    if (!base.isValid() || base.isRelative() || host.isEmpty() || base.hasQuery() || base.hasFragment()
        || base.authority().contains(QLatin1Char('@'))
        || (base.scheme() != QStringLiteral("https") && !(base.scheme() == QStringLiteral("http") && loopback))) {
        throw std::invalid_argument("Auth requires an HTTPS API base or demo loopback HTTP, without credentials, query or fragment.");
    }
    // La barra final conserva /api/v1 al resolver auth/login o users/me.
    if (!base.path().endsWith(QLatin1Char('/'))) {
        base.setPath(base.path() + QLatin1Char('/'));
    }
    return base.resolved(QUrl(relativePath));
}

AuthResult AuthSession::ReadResponseStatus(const RestResult& received) {
    AuthResult result;
    result.statusCode = received.statusCode;
    if (received.transportError != RestTransportError::None) {
        // Un fallo de transporte tiene prioridad aunque haya llegado un estado HTTP parcial.
        result.outcome = AuthOutcome::TransportError;
        result.transportError = received.transportError;
    } else if (received.statusCode && *received.statusCode >= 300) {
        result.outcome = AuthOutcome::HttpError;
        // Conserva el estado HTTP aunque el cuerpo carezca de un código de Problem Details.
        const QJsonValue code = QJsonDocument::fromJson(received.body).object().value(QStringLiteral("code"));
        if (code.isString() && !code.toString().trimmed().isEmpty()) {
            result.problemCode = code.toString();
        }
    }
    return result;
}

void AuthSession::RequireOwnerThread() const {
    // Rechaza la llamada antes de tocar tokens o generaciones, igual que el transporte protege su gestor Qt.
    if (QThread::currentThread() != ownerThread_) {
        throw std::logic_error("Auth session must be used from its owning Qt thread.");
    }
}

}  // namespace sbp
