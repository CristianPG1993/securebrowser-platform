#pragma once

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkRequest>

#include <functional>
#include <optional>

namespace sbp {

// Describe fallos del transporte; los estados HTTP como 401 o 503 se conservan aparte.
enum class RestTransportError {
    None,
    Network,
    Timeout,
    Cancelled
};

// Copia propia del resultado, sin conservar referencias a la respuesta de Qt Network.
struct RestResult {
    // Ausente si no se recibió un estado HTTP; puede coexistir con un fallo posterior de transporte.
    std::optional<int> statusCode;

    // Bytes recibidos, todavía sin interpretar como JSON ni asumir que el cuerpo está completo.
    // Puede contener tokens; no debe registrarse indiscriminadamente.
    QByteArray body;

    // None no implica éxito HTTP: también hay que comprobar el estado y validar el cuerpo.
    RestTransportError transportError = RestTransportError::None;
};

// Cliente que realizará las peticiones HTTP de la aplicación de forma asíncrona.
class RestClient final {
public:
    // Función que recibirá una copia del resultado cuando termine la petición.
    using CompletionCallback = std::function<void(RestResult)>;

    // Operaciones necesarias para consultar datos y enviar login, refresh, logout y eventos.
    enum class Method {
        Get,
        Post
    };

    // Construir desde el hilo principal de Qt, con QApplication ya creado.
    RestClient() = default;

    // Retira las entregas pendientes antes de destruir el gestor y sus respuestas.
    // Debe destruirse en el mismo hilo Qt donde se construyó.
    ~RestClient();

    // Inicia una petición sin esperar su respuesta; request contiene la URL y las cabeceras.
    // body contiene los bytes que se enviarán; onCompleted recibirá el resultado posteriormente.
    // Se utilizará en el mismo hilo Qt donde se construyó el cliente.
    // La callback se ejecutará en ese hilo; al destruir el cliente se retiran las entregas pendientes.
    // Exige callback, GET sin cuerpo y una URL HTTPS o HTTP loopback para la demo local.
    // Los argumentos incorrectos lanzan invalid_argument; otro hilo lanza logic_error.
    void Send(Method method, const QNetworkRequest& request,
              const QByteArray& body, CompletionCallback onCompleted);

    // Cancela las peticiones activas; sus callbacks reciben Cancelled, en este mismo hilo Qt.
    // El receptor debe descartar resultados cuyo contexto de sesión ya no sea el actual.
    void CancelAll();

private:
    // Obtiene las respuestas propias sin importar QNetworkReply::staticMetaObject como dato.
    [[nodiscard]] QList<QNetworkReply*> OwnedReplies() const;

    // Evita utilizar el gestor de Qt desde un hilo distinto del que lo posee.
    void RequireOwnerThread() const;

    // Valida la petición y fija JSON, acceso a red y redirecciones sin seguirlas automáticamente.
    [[nodiscard]] static QNetworkRequest PrepareRequest(
        Method method, const QNetworkRequest& request, const QByteArray& body);

    // Copia estado, cuerpo y error; no interpreta JSON ni decide si la operación fue autorizada.
    [[nodiscard]] static RestResult ReadResult(QNetworkReply& reply, bool timedOut);

    // Contiene las excepciones del receptor para que no atraviesen el bucle de eventos de Qt.
    static void DeliverResult(CompletionCallback onCompleted, RestResult result) noexcept;

    // Límite total de la petición, distinto de la vigencia local de la política.
    static constexpr int RequestTimeoutMilliseconds = 15000;

    // Gestiona la red de forma asíncrona y vive hasta que se destruye este cliente.
    QNetworkAccessManager networkManager_;
};

}  // namespace sbp
