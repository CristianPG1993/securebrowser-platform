#pragma once

#include "rest/rest_client.h"
#include "rest/token_response.h"

#include <QString>
#include <QThread>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace sbp {

// Resultado de una operación de autenticación, separado del estado de sesión y de la autorización de un Device.
enum class AuthOutcome {
    Success,
    HttpError,
    TransportError,
    InvalidResponse
};

// Información que recibirá quien solicite login, refresh o logout.
// Los tokens se gestionarán dentro de AuthSession; este resultado no expone secretos ni el cuerpo HTTP.
struct AuthResult {
    // Empieza sin éxito confirmado: solo la operación completada podrá indicar Success.
    AuthOutcome outcome = AuthOutcome::InvalidResponse;

    // Conserva el código HTTP cuando exista, incluso si un HTTP 200 contiene un JSON inválido.
    std::optional<int> statusCode;

    // Detalla desconexión, timeout o cancelación cuando outcome sea TransportError.
    RestTransportError transportError = RestTransportError::None;

    // Código opcional de Problem Details, como INVALID_CREDENTIALS; no contiene el mensaje libre del servidor.
    std::optional<QString> problemCode;
};

// Coordinará la autenticación mediante RestClient y conservará los tokens de la sesión en memoria.
// Se construirá y utilizará en el mismo hilo Qt que el transporte recibido.
class AuthSession final {
public:
    // Quien solicite la operación recibirá su resultado interpretado, sin tokens ni contraseña.
    using CompletionCallback = std::function<void(AuthResult)>;

    // client debe vivir más que esta sesión; la URL base se conserva por valor.
    // Ejemplo de base de la demo local: http://127.0.0.1:8081/api/v1/.
    AuthSession(RestClient& client, QUrl apiBaseUrl);

    // Una sesión tiene una única identidad local; no se duplica copiando este objeto.
    AuthSession(const AuthSession&) = delete;
    AuthSession& operator=(const AuthSession&) = delete;

    // Prepara el JSON y envía POST auth/login sin JWT; onCompleted se atiende en el hilo Qt.
    // La contraseña se utiliza para la petición, sin guardarla como estado de sesión.
    // Otro login invalida los tokens e intentos anteriores; sus respuestas se descartan sin llamar al receptor.
    // Destruir AuthSession también retira sus entregas. Callback o URL base incorrectas lanzan invalid_argument.
    void Login(const QString& email, const QString& password, CompletionCallback onCompleted);

private:
    // Se llamará antes de leer o modificar la sesión; otro hilo constituye un uso incorrecto de la clase.
    void RequireOwnerThread() const;

    // Estado privado de una sesión; se consultará y modificará únicamente desde su hilo Qt.
    struct State {
        // Identificador local de intento/contexto, distinto del ID de usuario o Device del backend.
        // Se incrementará al iniciar otro login o retirar la sesión para invalidar respuestas anteriores.
        std::uint64_t generation = 0;

        // Vacío hasta aceptar un TokenResponse completo; ambos tokens se conservan y sustituyen juntos.
        std::optional<TokenResponse> tokens;
    };

    // Referencia no propietaria al transporte HTTP; su creación y destrucción pertenecen a la composición.
    RestClient& client_;

    // Dirección base del backend que se utilizará para construir las rutas de autenticación.
    QUrl apiBaseUrl_;

    // AuthSession conservará el estado; las callbacks de red capturarán weak_ptr para comprobar si sigue vivo.
    // Al destruir la sesión, esas callbacks podrán descartar su resultado sin acceder al objeto destruido.
    std::shared_ptr<State> state_;

    // Identidad del hilo Qt de construcción. AuthSession debe vivir dentro de la vida útil de ese hilo.
    QThread* const ownerThread_;
};

}  // namespace sbp
