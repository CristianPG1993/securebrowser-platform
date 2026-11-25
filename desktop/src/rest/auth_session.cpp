#include "rest/auth_session.h"
#include "rest/device_page_parser.h"
#include "rest/token_response_parser.h"
#include "rest/user_identity_parser.h"

#include <QHttpHeaders>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QUuid>

#include <cstdio>
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
    // Cada intento retira el par anterior y recibe una generación no reutilizada.
    const std::uint64_t attempt = ResetSession();
    state_->logoutRequested = false;
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

void AuthSession::Logout(CompletionCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("Auth logout requires a completion callback.");
    }

    // El POST necesita el refreshToken que pertenece al login que estamos cerrando.
    const QString refreshToken = state_->tokens ? state_->tokens->refreshToken : QString{};
    if (!state_->logoutRequested) {
        // También retira un Login en curso que todavía no ha entregado sus tokens.
        ResetSession();
        state_->logoutRequested = true;
    }
    if (refreshToken.isEmpty()) {
        AuthResult result;
        result.outcome = AuthOutcome::NoSession;
        onCompleted(std::move(result));
        return;
    }

    QNetworkRequest request(EndpointUrl(QStringLiteral("auth/logout")));
    const QJsonObject payload{{QStringLiteral("refreshToken"), refreshToken}};
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    const auto generation = state_->generation;
    const std::weak_ptr<State> weakState(state_);
    // RestClient entrega la respuesta en el hilo Qt. Otro Login o la destrucción de la sesión retiran esta entrega.
    client_.Send(RestClient::Method::Post, request, body,
        [weakState, generation, onCompleted = std::move(onCompleted)](RestResult received) {
            if (const auto state = weakState.lock(); !state || state->generation != generation) {
                return;
            }
            AuthResult result = ReadResponseStatus(received);
            if (result.transportError == RestTransportError::None && result.statusCode == 204 && received.body.isEmpty()) {
                result.outcome = AuthOutcome::Success;
            }
            onCompleted(std::move(result));
        });
}

void AuthSession::FetchUserIdentity(IdentityCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("User identity request requires a completion callback.");
    }

    // El GET entrega su resultado en el hilo de la sesión. Solo un perfil válido llega al receptor como Success.
    SendAuthenticatedGet(QStringLiteral("users/me"),
        [onCompleted = std::move(onCompleted)](AuthResult result, QByteArray body) {
            std::optional<UserIdentity> identity;
            if (result.outcome == AuthOutcome::Success) {
                identity = UserIdentityParser::Parse(body);
                if (!identity) {
                    result.outcome = AuthOutcome::InvalidResponse;
                }
            }
            onCompleted(std::move(result), std::move(identity));
        }, true);
}

void AuthSession::FetchDeviceIdentity(QString deviceIdentifier, DeviceIdentityCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("Device identity request requires a completion callback.");
    }
    if (QUuid::fromString(deviceIdentifier).toString(QUuid::WithoutBraces) != deviceIdentifier) {
        throw std::invalid_argument("Device identity request requires a canonical UUID.");
    }

    // FetchUserIdentity entrega el perfil en el hilo Qt mientras siga vigente el mismo login.
    FetchUserIdentity([this, deviceIdentifier = std::move(deviceIdentifier), onCompleted = std::move(onCompleted)]
                      (AuthResult result, std::optional<UserIdentity> owner) mutable {
        if (!owner) {
            onCompleted(std::move(result), std::nullopt);
            return;
        }
        FetchDevicePage(std::move(deviceIdentifier), *owner, 0, std::move(onCompleted));
    });
}

void AuthSession::FetchDevicePage(QString deviceIdentifier, UserIdentity owner, int page, DeviceIdentityCallback onCompleted) {
    const QString path = QStringLiteral("devices?page=%1&userId=%2").arg(page).arg(owner.id);
    // El GET descarta las respuestas de sesiones retiradas. El UUID y el perfil viajan por valor entre páginas.
    SendAuthenticatedGet(path,
        [this, deviceIdentifier = std::move(deviceIdentifier), owner, page, onCompleted = std::move(onCompleted)]
        (AuthResult result, QByteArray body) mutable {
            if (result.outcome != AuthOutcome::Success) {
                onCompleted(std::move(result), std::nullopt);
                return;
            }
            const auto devices = DevicePageParser::Parse(body);
            if (!devices || devices->page != page) {
                result.outcome = AuthOutcome::InvalidResponse;
                onCompleted(std::move(result), std::nullopt);
                return;
            }

            for (const DeviceIdentity& device : devices->items) {
                if (device.deviceIdentifier == deviceIdentifier && device.userId == owner.id
                    && device.companyId == owner.companyId) {
                    onCompleted(std::move(result), device);
                    return;
                }
            }
            if (page < devices->totalPages - 1) {
                FetchDevicePage(std::move(deviceIdentifier), owner, page + 1, std::move(onCompleted));
                return;
            }
            // Se leyó la última página y la instalación no figura entre los dispositivos de este usuario.
            onCompleted(std::move(result), std::nullopt);
        }, true);
}

void AuthSession::SendAuthenticatedGet(QString relativePath, GetCallback onCompleted, bool allowRefresh) {
    if (!state_->tokens) {
        AuthResult result;
        result.outcome = AuthOutcome::NoSession;
        onCompleted(std::move(result), {});
        return;
    }

    // Si el POST de renovación ya salió, esta consulta espera el nuevo par antes de enviar su GET.
    if (allowRefresh && !state_->pendingRefresh.expired()) {
        SendAuthenticatedGetAfterRefresh(std::move(relativePath), std::move(onCompleted));
        return;
    }

    QNetworkRequest request(EndpointUrl(relativePath));
    QHttpHeaders headers;
    const QByteArray bearer = "Bearer " + state_->tokens->accessToken.toUtf8();
    if (!headers.append(QHttpHeaders::WellKnownHeader::Authorization, bearer)) {
        // Si Qt rechaza el valor de la cabecera, la petición termina aquí con InvalidResponse.
        onCompleted(AuthResult{}, {});
        return;
    }
    request.setHeaders(std::move(headers));

    // El perfil y cada página de dispositivos pertenecen a la generación que abrió Login.
    const auto generation = state_->generation;
    const auto tokenRevision = state_->tokenRevision;
    const std::weak_ptr<State> weakState(state_);
    // RestClient entrega el GET en el hilo Qt. Antes de usar this, comprobamos que la sesión sigue viva.
    client_.Send(RestClient::Method::Get, request, {},
        [this, weakState, generation, tokenRevision, allowRefresh, relativePath = std::move(relativePath),
         onCompleted = std::move(onCompleted)](RestResult received) mutable {
            if (const auto state = weakState.lock(); !state || state->generation != generation || !state->tokens) {
                return;
            }

            AuthResult result = ReadResponseStatus(received);
            if (allowRefresh && result.outcome == AuthOutcome::HttpError && result.statusCode == 401
                && result.problemCode == QStringLiteral("AUTHENTICATION_REQUIRED")) {
                if (state_->tokenRevision == tokenRevision) {
                    SendAuthenticatedGetAfterRefresh(std::move(relativePath), std::move(onCompleted));
                } else {
                    // Otra petición ya renovó el par mientras este GET esperaba su respuesta.
                    SendAuthenticatedGet(std::move(relativePath), std::move(onCompleted), false);
                }
                return;
            }
            if (result.transportError == RestTransportError::None && result.statusCode == 200) {
                result.outcome = AuthOutcome::Success;
            }
            onCompleted(std::move(result), std::move(received.body));
        });
}

void AuthSession::SendAuthenticatedGetAfterRefresh(QString relativePath, GetCallback onCompleted) {
    // Refresh llama en el hilo Qt y comprueba que la sesión sigue viva y pertenece al mismo login.
    Refresh([this, relativePath = std::move(relativePath), onCompleted = std::move(onCompleted)](AuthResult result) mutable {
        if (result.outcome == AuthOutcome::Success) {
            SendAuthenticatedGet(std::move(relativePath), std::move(onCompleted), false);
        } else {
            onCompleted(std::move(result), {});
        }
    });
}

void AuthSession::Refresh(CompletionCallback onCompleted) {
    RequireOwnerThread();
    if (!onCompleted) {
        throw std::invalid_argument("Auth refresh requires a completion callback.");
    }
    if (!state_->tokens) {
        AuthResult result;
        result.outcome = AuthOutcome::NoSession;
        onCompleted(std::move(result));
        return;
    }

    // Un refreshToken se consume al usarlo. Si el POST ya salió, esta llamada espera su resultado.
    if (auto pending = state_->pendingRefresh.lock()) {
        pending->push_back(std::move(onCompleted));
        return;
    }

    QNetworkRequest request(EndpointUrl(QStringLiteral("auth/refresh")));
    const QJsonObject payload{{QStringLiteral("refreshToken"), state_->tokens->refreshToken}};
    const QByteArray body = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    auto callbacks = std::make_shared<std::vector<CompletionCallback>>();
    callbacks->push_back(std::move(onCompleted));
    state_->pendingRefresh = callbacks;

    const std::weak_ptr<State> weakState(state_);
    const auto generation = state_->generation;
    // RestClient conserva la lista en esta lambda y entrega la respuesta en el hilo Qt de la sesión.
    // Si Send falla al iniciar la petición, su lista se libera y una llamada posterior puede volver a renovar.
    client_.Send(RestClient::Method::Post, request, body, [weakState, generation, callbacks](RestResult received) {
        auto state = weakState.lock();
        if (!state || state->generation != generation) {
            return;
        }
        state->pendingRefresh.reset();
        AuthResult result = ReadResponseStatus(received);
        if (result.transportError == RestTransportError::None && result.statusCode == 200) {
            auto tokens = TokenResponseParser::Parse(received.body);
            // El backend rota el refreshToken. Solo el par completo y renovado sustituye al anterior.
            if (tokens && state->tokens && tokens->refreshToken != state->tokens->refreshToken) {
                state->tokens = std::move(*tokens);
                ++state->tokenRevision;
                result.outcome = AuthOutcome::Success;
            }
        } else if (result.outcome == AuthOutcome::HttpError && result.statusCode == 401
                   && result.problemCode == QStringLiteral("INVALID_REFRESH_TOKEN")) {
            // Este rechazo confirma que hace falta un nuevo login. Un fallo temporal conserva la sesión.
            state->tokens.reset();
        }

        const bool hasTokens = state->tokens.has_value();
        state.reset();
        for (auto& callback : *callbacks) {
            // Un receptor anterior puede destruir la sesión, iniciar otro login o perderla al procesar eventos.
            if (const auto current = weakState.lock();
                !current || current->generation != generation || current->tokens.has_value() != hasTokens) {
                return;
            }
            try {
                callback(result);
            } catch (...) {
                // Un fallo de un receptor permite continuar con los demás. El mensaje omite los datos de la sesión.
                std::fputs("Auth refresh completion callback failed.\n", stderr);
            }
        }
    });
}

std::uint64_t AuthSession::ResetSession() {
    if (state_->generation == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error("Auth session generations exhausted.");
    }
    state_->tokens.reset();
    state_->tokenRevision = 0;
    state_->pendingRefresh.reset();
    return ++state_->generation;
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
