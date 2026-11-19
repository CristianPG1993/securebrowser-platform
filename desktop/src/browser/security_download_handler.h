#pragma once

#include "browser/download_decision_notice.h"
#include "browser/download_result_notice.h"
#include "security/download_policy.h"
#include "security/policy_manager.h"
#include "security/security_context.h"

#include "include/cef_download_handler.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace sbp {

// Conecta los callbacks de descarga de CEF con el estado local de seguridad.
// Los callbacks de descarga se ejecutan en el hilo UI de CEF; los diálogos pertenecen a Qt.
class SecurityDownloadHandler final : public CefDownloadHandler {
public:
    // Qt invocará esta respuesta con la ruta absoluta UTF-8 elegida o nullopt si el usuario cancela.
    // El handler proporcionará una función que encole la resolución en el hilo UI de CEF.
    // La cancelación voluntaria del diálogo no representa un bloqueo de política.
    using SaveDialogResultCallback = std::function<void(std::optional<std::string>)>;

    // Recibe el identificador de operación, el nombre sugerido UTF-8 y la función de respuesta.
    // Se invoca en UI CEF; el receptor encola la petición para Qt sin esperar al usuario.
    // true indica que se encoló la petición, no que el destino o la descarga estén autorizados.
    // false indica un fallo de entrega, distinto de una cancelación voluntaria o bloqueo de política.
    using RequestSaveDialogCallback =
        std::function<bool(std::uint64_t, std::string, SaveDialogResultCallback)>;

    // CEF solicita retirar el diálogo de una operación cuyo destino ya no está pendiente.
    // Se invoca en UI CEF; el receptor encolará su cierre en Qt usando el identificador propio.
    using RequestCloseSaveDialogCallback = std::function<void(std::uint64_t)>;

    // Recibe en UI CEF el bloqueo aplicado; el receptor debe copiarlo y encolar su presentación en Qt.
    // true confirma la aceptación del aviso, sin autorizar ni reanudar la descarga bloqueada.
    using DecisionCallback = std::function<bool(const DownloadDecisionNotice&)>;

    // Recibe en UI CEF una finalización o fallo; el receptor copia el aviso y lo encola para Qt.
    // No se utiliza para bloqueos de política ni cancelaciones voluntarias.
    using ResultCallback = std::function<bool(const DownloadResultNotice&)>;

    // Comparte el gestor existente y lo mantiene vivo mientras el handler lo utiliza.
    // Consultará la política vigente, sin conservar una copia fija de sus reglas.
    // El receptor encolará las peticiones de guardado para Qt desde el hilo UI de CEF.
    // Si está vacío, no se podrá solicitar un destino y la solicitud deberá cancelarse.
    explicit SecurityDownloadHandler(
        std::shared_ptr<PolicyManager> policyManager,
        RequestSaveDialogCallback onRequestSaveDialog = {},
        RequestCloseSaveDialogCallback onRequestCloseSaveDialog = {},
        DecisionCallback onDecision = {},
        ResultCallback onDownloadResult = {});

    // Las referencias CefRefPtr comparten el handler; el objeto no se copia.
    SecurityDownloadHandler(const SecurityDownloadHandler&) = delete;
    SecurityDownloadHandler& operator=(const SecurityDownloadHandler&) = delete;

    // Construye el receptor que encolará en UI CEF la cancelación de las descargas del contexto retirado.
    // La función conserva el handler; la sesión mantendrá la suscripción fuera de este objeto.
    // Crear el receptor no lo registra ni cancela todavía ninguna descarga.
    [[nodiscard]] PolicyManager::ContextRetiredCallback MakeContextRetiredHandler();

    // CEF lo llama en su hilo UI antes de descargas iniciadas por una acción del usuario.
    // true permite continuar con las comprobaciones posteriores; false cancela la descarga.
    // Solo comprueba contexto y política: no recibe el nombre ni autoriza el destino final.
    // StartDownload no pasa por este callback; OnBeforeDownload mantendrá el control posterior.
    bool CanDownload(CefRefPtr<CefBrowser> browser,
                     const CefString& url,
                     const CefString& requestMethod) override;

    // Recibe el nombre sugerido en el hilo UI de CEF, antes de autorizar la continuación.
    // true asume la gestión de la descarga; false delegaría en el comportamiento predeterminado.
    // El handler evaluará el nombre sugerido y el definitivo antes de Continue(ruta, false).
    // Con true, liberar callback sin ejecutarlo cancela; no se conserva downloadItem fuera del método.
    bool OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefDownloadItem> downloadItem,
                          const CefString& suggestedName,
                          CefRefPtr<CefBeforeDownloadCallback> callback) override;

    // CEF comunica cambios de estado y progreso en su hilo UI, antes o después de OnBeforeDownload.
    // Permite solicitar Cancel mediante callback y retirar el seguimiento al terminar la descarga.
    // No se conserva downloadItem fuera del método ni se reevalúan reglas de transferencias autorizadas.
    // El fin de contexto o la retirada de autorización sí obligarán a cancelar las transferencias activas.
    void OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefDownloadItem> downloadItem,
                           CefRefPtr<CefDownloadItemCallback> callback) override;

    // Cancela solicitudes pendientes y transferencias activas desde el hilo UI de CEF.
    // Se invocará al cerrar el navegador o retirar el contexto o su autorización.
    // Retirará las continuaciones pendientes para descartar respuestas tardías del diálogo Qt.
    void CancelAll();

private:
    // La última referencia CefRefPtr destruye el handler; el código externo no hace delete.
    ~SecurityDownloadHandler() override = default;

    // Identifica la descarga mediante los IDs de CEF: primero navegador y después descarga.
    using DownloadKey = std::pair<int, std::uint32_t>;

    // Describe la autorización local; el estado real de la transferencia se consulta en CEF.
    enum class DownloadPhase {
        // Todavía no se ha autorizado la continuación con un destino definitivo.
        AwaitingAuthorization,

        // La continuación ya se autorizó; un cambio o caducidad de política no la cancela por sí solo.
        Authorized,

        // Se ha decidido cancelar; queda pendiente que CEF comunique el estado final.
        CancelRequested
    };

    // Conserva una solicitud pendiente de destino; solo se utiliza en el hilo UI de CEF.
    // Qt recibirá datos y una función de respuesta, sin referencias a objetos CEF.
    struct PendingDownload {
        // Identificador propio para correlacionar la respuesta Qt y descartar respuestas tardías.
        std::uint64_t operationId;

        // Copia del contexto que inició la solicitud; no se sustituye por el de otra sesión.
        SecurityContext context;

        // Mantiene la continuación en CEF mientras se elige y comprueba el destino.
        // Liberarlo sin Continue después de devolver true en OnBeforeDownload cancela la solicitud.
        CefRefPtr<CefBeforeDownloadCallback> callback;
    };

    // Sigue la descarga desde su primer callback hasta el estado final, solo en el hilo UI de CEF.
    // Conserva datos propios y el callback de cancelación, sin retener CefDownloadItem.
    struct TrackedDownload {
        // Identifica la misma operación que PendingDownload mientras haya un destino pendiente.
        std::uint64_t operationId;

        // nullopt indica que la descarga se observó sin contexto; no se le asignará otra sesión después.
        std::optional<SecurityContext> context;

        // Se recibe en OnDownloadUpdated y permite cancelar pendientes o transferencias activas.
        CefRefPtr<CefDownloadItemCallback> cancelCallback;

        // Empieza sin autorización; se actualizará al continuar o solicitar la cancelación.
        DownloadPhase phase = DownloadPhase::AwaitingAuthorization;

        // Evita repetir el resultado cuando CEF comunica varias actualizaciones del mismo fallo.
        bool resultReported = false;
    };

    // Asigna un identificador nuevo desde el hilo UI de CEF, sin reutilizar los anteriores.
    // Devuelve nullopt si se agota el contador; la solicitud deberá rechazarse en ese caso.
    [[nodiscard]] static std::optional<std::uint64_t> AllocateOperationId();

    // Obtiene o crea el seguimiento en UI CEF, conservando el ID y contexto de su primera observación.
    // Devuelve nullptr si no puede asignar un identificador nuevo.
    // El puntero solo es válido mientras la entrada permanezca en trackedDownloads_.
    [[nodiscard]] TrackedDownload* GetOrCreateTrackedDownload(DownloadKey key);

    // Solicita cancelar una descarga concreta desde el hilo UI de CEF.
    // Retira su continuación pendiente y marca el seguimiento como CancelRequested.
    // Usa el callback de cancelación si está disponible; conserva el seguimiento hasta el estado final.
    void CancelDownload(DownloadKey key);

    // Atiende la retirada en UI CEF sin esperar otra actualización nativa de progreso.
    // Cancela pendientes y activas del contexto recibido, sin afectar a las de un contexto posterior.
    void CancelContextDownloads(const SecurityContext& context);

    // Libera la continuación y solicita Cancel en otra tarea UI CEF, fuera de OnDownloadUpdated.
    // El destructor de la continuación puede cancelar de forma síncrona y actualizar observadores nativos.
    static void QueueCallbackCancellation(CefRefPtr<CefBeforeDownloadCallback> pendingCallback,
                                          CefRefPtr<CefDownloadItemCallback> cancelCallback);

    // Prepara el aviso antes de cancelar, conservando el ID y el contexto originales aunque CEF retire el registro.
    // Solo comunica el primer bloqueo de una operación; una cancelación ya solicitada no genera otro aviso.
    // state corresponde a la evaluación aplicada; no se atribuirá la política de otro contexto al bloqueo.
    void BlockDownload(DownloadKey key, SecurityReason reason,
                       const std::shared_ptr<const PolicyManager::State>& state);

    // Copia y consume el resultado antes de cancelar o retirar entradas; nunca publica otro contexto.
    [[nodiscard]] std::optional<DownloadResultNotice> TakeResultNotice(DownloadKey key, DownloadOutcome outcome);
    // Cancela por un fallo operativo y entrega su resultado una sola vez, sin emitir un bloqueo de política.
    void FailDownload(DownloadKey key, DownloadOutcome outcome);
    // Atiende interrupciones recibidas antes o después de elegir destino, manteniendo silencioso Cancelar.
    void HandleInterruptedDownload(DownloadKey key, cef_download_interrupt_reason_t reason);
    // La entrega no modifica la descarga; una excepción del receptor no atraviesa los callbacks de CEF.
    void NotifyResult(const DownloadResultNotice& notice);
    // Traduce códigos nativos a categorías propias; Qt no necesita depender de CefDownloadItem.
    [[nodiscard]] static DownloadOutcome InterruptionOutcome(cef_download_interrupt_reason_t reason);

    // Extrae el nombre de una ruta absoluta UTF-8 sin acceder al disco ni modificar el destino.
    // Devuelve nullopt si la ruta no puede interpretarse, no es absoluta o no contiene un nombre.
    // DownloadPolicy validará después el nombre separado de la carpeta.
    [[nodiscard]] static std::optional<std::string> ExtractDestinationFileName(
        const std::string& path);

    // Resuelve la respuesta del diálogo en el hilo UI de CEF, con una ruta absoluta UTF-8 o nullopt.
    // Descarta respuestas cuya clave o identificador ya no correspondan a una solicitud pendiente.
    // Al confirmar, reevalúa el nombre definitivo con la política vigente para el contexto de origen.
    // nullopt retira la solicitud por cancelación voluntaria, sin aviso de bloqueo de política.
    void CompleteSaveDialog(DownloadKey key,
                            std::uint64_t operationId,
                            std::optional<std::string> path);

    // Entrega el aviso desde UI CEF; un fallo o una excepción del receptor nunca convierten el bloqueo en permiso.
    void NotifyBlocked(const DownloadDecisionNotice& notice);

    // Mantiene el mismo gestor que utiliza la sesión y el handler de navegación.
    const std::shared_ptr<PolicyManager> policyManager_;

    // Se fija al construir el handler; solo se invocará desde el hilo UI de CEF.
    const RequestSaveDialogCallback onRequestSaveDialog_;

    // Avisa a Qt al retirar un destino pendiente; el cierre no puede resolver otra operación posterior.
    const RequestCloseSaveDialogCallback onRequestCloseSaveDialog_;

    // Se fija al construir el handler y recibe solo bloqueos; cancelar voluntariamente no genera este aviso.
    const DecisionCallback onDecision_;

    // Opcional para los clientes sin UI; no conserva widgets ni callbacks nativos de descarga.
    const ResultCallback onDownloadResult_;

    // Reutiliza el evaluador puro para los nombres sugerido y definitivo, sin duplicar sus reglas.
    DownloadPolicy downloadPolicy_;

    // Conserva las solicitudes pendientes hasta resolverlas o cancelarlas; solo se accede en UI CEF.
    // La retirada de una entrada libera la referencia a su callback de continuación.
    std::map<DownloadKey, PendingDownload> pendingDownloads_;

    // Sigue cada descarga hasta su estado final, incluso antes de recibir OnBeforeDownload.
    // Solo se accede en UI CEF; la entrada conserva el contexto original y el callback de cancelación.
    std::map<DownloadKey, TrackedDownload> trackedDownloads_;

    // Compartido entre handlers de descarga y modificado únicamente desde el hilo UI de CEF.
    // No se reinicia al retirar solicitudes o destruir una instancia del handler.
    static std::uint64_t lastOperationId_;

    // Añade el contador de referencias requerido por CEF.
    IMPLEMENT_REFCOUNTING(SecurityDownloadHandler);
};

}  // namespace sbp
