#pragma once

#include "rest/device_identity.h"
#include "rest/rest_client.h"
#include "rest/token_response.h"
#include "rest/user_identity.h"

#include <QString>
#include <QThread>
#include <QUrl>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace sbp {

// Resultado de una operación de autenticación, separado del estado de sesión y de la autorización de un Device.
enum class AuthOutcome {
    Success,
    HttpError,
    TransportError,
    InvalidResponse,
    // Una consulta protegida requiere que la sesión tenga tokens.
    NoSession
};

// Resultado de las operaciones de autenticación y de las consultas de identidad.
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

    // La consulta del perfil entrega su resultado y, solo en caso de éxito, la identidad del usuario.
    using IdentityCallback = std::function<void(AuthResult, std::optional<UserIdentity>)>;

    // Entrega el estado de la consulta y, si se encontró la instalación, los datos de su dispositivo.
    using DeviceIdentityCallback = std::function<void(AuthResult, std::optional<DeviceIdentity>)>;

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

    // Cierra la sesión local y usa su refreshToken para solicitar el cierre al backend con POST /auth/logout.
    // Sin tokens, entrega NoSession en esta llamada. El backend confirma el cierre con un 204 sin cuerpo.
    // Un fallo remoto se entrega al callback y la sesión local sigue cerrada.
    void Logout(CompletionCallback onCompleted);

    // Consulta GET /users/me con el accessToken de la sesión. Los fallos locales previos al envío se entregan
    // en esta misma llamada. Sin tokens, el resultado es NoSession y la identidad está vacía.
    // Ante 401 AUTHENTICATION_REQUIRED puede renovar los tokens y repetir el GET una sola vez.
    // Si la sesión cambia mientras llega la respuesta, descarta la entrega de la sesión anterior.
    void FetchUserIdentity(IdentityCallback onCompleted);

    // Busca el UUID de la instalación en las páginas de GET /devices del usuario autenticado.
    // Contrasta userId y companyId con su perfil de GET /users/me. Una búsqueda válida sin coincidencia
    // entrega Success y std::nullopt. Un dispositivo encontrado conserva su valor de active.
    // El UUID debe ser canónico (minúsculas, con guiones y sin llaves) o se lanza invalid_argument.
    void FetchDeviceIdentity(QString deviceIdentifier, DeviceIdentityCallback onCompleted);

private:
    // Un GET protegido entrega el cuerpo al lector del endpoint cuando recibe HTTP 200.
    using GetCallback = std::function<void(AuthResult, QByteArray)>;
    void SendAuthenticatedGet(QString relativePath, GetCallback onCompleted, bool allowRefresh);
    void SendAuthenticatedGetAfterRefresh(QString relativePath, GetCallback onCompleted);

    // Conserva el perfil comprobado y el UUID mientras recorre las páginas del mismo usuario.
    void FetchDevicePage(QString deviceIdentifier, UserIdentity owner, int page, DeviceIdentityCallback onCompleted);

    // Renueva la sesión con POST /auth/refresh usando el refreshToken guardado.
    // Las peticiones de la misma sesión que necesiten renovar a la vez compartirán una sola renovación.
    // Tras un 200 con tokens válidos, la sesión sustituirá el par y entregará el AuthResult a cada callback.
    void Refresh(CompletionCallback onCompleted);

    // Se llamará antes de leer o modificar la sesión; otro hilo constituye un uso incorrecto de la clase.
    void RequireOwnerThread() const;
    // Retira los tokens y la renovación pendiente. Devuelve la generación que sustituye a la anterior.
    std::uint64_t ResetSession();

    // Valida la base y conserva su ruta al añadir un endpoint relativo de la API.
    [[nodiscard]] QUrl EndpointUrl(const QString& relativePath) const;
    // Recoge errores HTTP y de transporte. Cada operación confirma el éxito tras validar su propio cuerpo.
    [[nodiscard]] static AuthResult ReadResponseStatus(const RestResult& received);

    // Estado privado de una sesión; se consultará y modificará únicamente desde su hilo Qt.
    struct State {
        // Identificador local de intento/contexto, distinto del ID de usuario o Device del backend.
        // Se incrementará al iniciar otro login o retirar la sesión para invalidar respuestas anteriores.
        std::uint64_t generation = 0;

        // Un segundo Logout local conserva la entrega del primer POST de cierre que aún espera respuesta.
        bool logoutRequested = false;

        // Vacío hasta aceptar un TokenResponse completo; ambos tokens se conservan y sustituyen juntos.
        std::optional<TokenResponse> tokens;

        // Cuenta las renovaciones aceptadas desde Login para reconocer un GET enviado con el par anterior.
        std::uint64_t tokenRevision = 0;

        // La petición de refresh posee esta lista mientras está en curso.
        // Las demás peticiones de la misma sesión pueden añadir su callback a la lista.
        std::weak_ptr<std::vector<CompletionCallback>> pendingRefresh;
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
