#include "browser/security_download_handler.h"

#include "include/base/cef_callback.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace sbp {

// Empieza en cero para que el primer identificador asignado sea uno.
std::uint64_t SecurityDownloadHandler::lastOperationId_ = 0;

// Conserva el gestor y los receptores de diálogo y bloqueo; construir el handler no ejecuta esas funciones.
SecurityDownloadHandler::SecurityDownloadHandler(std::shared_ptr<PolicyManager> policyManager,
                                                 RequestSaveDialogCallback onRequestSaveDialog,
                                                 RequestCloseSaveDialogCallback onRequestCloseSaveDialog,
                                                 DecisionCallback onDecision)
    : policyManager_(std::move(policyManager)),
      onRequestSaveDialog_(std::move(onRequestSaveDialog)),
      onRequestCloseSaveDialog_(std::move(onRequestCloseSaveDialog)),
      onDecision_(std::move(onDecision)) {
    if (!policyManager_) {
        throw std::invalid_argument("SecurityDownloadHandler requires a policy manager.");
    }
}

// Aplica la comprobación inicial de autorización y política antes de evaluar el nombre.
bool SecurityDownloadHandler::CanDownload(CefRefPtr<CefBrowser> browser,
                                        const CefString& /*url*/,
                                        const CefString& /*requestMethod*/) {
    CEF_REQUIRE_UI_THREAD();
    if (!browser) {
        return false;
    }

    const auto state = policyManager_->ReadState();
    // El gestor comprueba de nuevo ese contexto y su vigencia sin renovar el plazo.
    const auto policy = state ? policyManager_->ReadPolicy(state->context)
                              : PolicyManager::PolicyReadResult{};

    // Solo permite pasar a las comprobaciones posteriores del nombre y del destino final.
    if (policy.availability == PolicyAvailability::Available) {
        return true;
    }

    // Traduce el rechazo inicial sin evaluar un nombre que este callback todavía no recibe.
    SecurityReason reason = SecurityReason::PolicyUnavailable;
    if (policy.availability == PolicyAvailability::InvalidContext) {
        reason = SecurityReason::InvalidContext;
    } else if (policy.availability == PolicyAvailability::Expired) {
        reason = SecurityReason::PolicyExpired;
    }

    const auto operationId = AllocateOperationId();
    if (!operationId.has_value()) {
        std::fputs("CEF download blocked; decision correlation counter exhausted.\n", stderr);
        return false;
    }

    // No hay CefDownloadItem ni seguimiento todavía; downloadId permanece sin valor.
    DownloadDecisionNotice notice;
    notice.operationId = *operationId;
    notice.occurredAt = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now());
    notice.browserId = browser->GetIdentifier();
    notice.decision = SecurityDecision(reason);

    // Si terminó el contexto durante la lectura, conserva su origen sin tomar datos de otra sesión.
    const auto evaluatedState = policy.state ? policy.state : state;
    if (evaluatedState) {
        notice.context = evaluatedState->context;
        if (evaluatedState->policy) {
            notice.policyId = evaluatedState->policy->Id();
        }
    }

    // false aplica el rechazo en este callback; el resultado del receptor no puede convertirlo en permiso.
    NotifyBlocked(notice);
    return false;
}

// Comprueba el nombre sugerido antes de pedir a Qt un destino editable.
bool SecurityDownloadHandler::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                               CefRefPtr<CefDownloadItem> downloadItem,
                                               const CefString& suggestedName,
                                               CefRefPtr<CefBeforeDownloadCallback> callback) {
    CEF_REQUIRE_UI_THREAD();

    // true asume la gestión; liberar la continuación sin ejecutarla cancela en ambos estilos de CEF.
    if (!browser || !downloadItem || !downloadItem->IsValid() || !callback) {
        return true;
    }

    const DownloadKey key{browser->GetIdentifier(), downloadItem->GetId()};
    TrackedDownload* tracked = GetOrCreateTrackedDownload(key);
    if (!tracked) {
        return true;
    }

    // No sustituye el origen de una descarga observada antes ni abre otro diálogo para la misma solicitud.
    if (tracked->phase != DownloadPhase::AwaitingAuthorization ||
        pendingDownloads_.find(key) != pendingDownloads_.end() ||
        downloadItem->IsComplete() || downloadItem->IsCanceled() || downloadItem->IsInterrupted()) {
        CancelDownload(key);
        return true;
    }

    // Un origen sin contexto es un bloqueo de seguridad, también para StartDownload, y se comunica con su ID.
    if (!tracked->context.has_value()) {
        BlockDownload(key, SecurityReason::InvalidContext, nullptr);
        return true;
    }

    // También cubre StartDownload, que no pasa por CanDownload, utilizando el evaluador existente.
    const std::string fileName = suggestedName.ToString();
    const auto policy = policyManager_->ReadPolicy(*tracked->context);
    const SecurityDecision decision = downloadPolicy_.Evaluate(policy, fileName);
    if (decision.Action() != SecurityAction::Allow) {
        // Cancela y conserva el motivo del evaluador antes de solicitar cualquier diálogo Qt.
        BlockDownload(key, decision.Reason(), policy.state);
        return true;
    }

    // Sin receptor no se puede elegir un destino; este fallo de entrega no es un bloqueo de política.
    if (!onRequestSaveDialog_) {
        CancelDownload(key);
        return true;
    }

    // Registra la continuación antes de avisar a Qt y copia el ID para no reutilizar el puntero después.
    const std::uint64_t operationId = tracked->operationId;
    pendingDownloads_.emplace(
        key, PendingDownload{operationId, *tracked->context, std::move(callback)});

    // Qt solo entrega una ruta o nullopt: esta función conserva el handler y encola la resolución en CEF.
    SaveDialogResultCallback onResult =
        [handler = CefRefPtr<SecurityDownloadHandler>(this), key, operationId](std::optional<std::string> path) {
            if (!CefPostTask(TID_UI, base::BindOnce(&SecurityDownloadHandler::CompleteSaveDialog,
                                                  handler, key, operationId, std::move(path)))) {
                std::fputs("Qt save dialog response could not be posted to CEF.\n", stderr);
            }
        };

    // La entrega no autoriza la descarga: CompleteSaveDialog comprobará el nombre definitivo y el contexto.
    if (!onRequestSaveDialog_(operationId, fileName, std::move(onResult))) {
        CancelDownload(key);
    }
    return true;
}

// Actualiza el seguimiento sin conservar downloadItem fuera de esta llamada de CEF.
void SecurityDownloadHandler::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                              CefRefPtr<CefDownloadItem> downloadItem,
                                              CefRefPtr<CefDownloadItemCallback> callback) {
    CEF_REQUIRE_UI_THREAD();

    // Sin datos válidos no se puede identificar ni comprobar el origen de la descarga.
    if (!browser || !downloadItem || !downloadItem->IsValid()) {
        if (callback) {
            callback->Cancel();
        }
        return;
    }

    const DownloadKey key{browser->GetIdentifier(), downloadItem->GetId()};
    if (downloadItem->IsComplete() || downloadItem->IsCanceled()) {
        // Retira ambos registros antes de liberar una posible continuación pendiente.
        // Una respuesta posterior de Qt ya no encontrará la solicitud original.
        CefRefPtr<CefBeforeDownloadCallback> pendingCallback;
        std::optional<std::uint64_t> pendingOperationId;
        const auto pending = pendingDownloads_.find(key);
        if (pending != pendingDownloads_.end()) {
            pendingOperationId = pending->second.operationId;
            pendingCallback = std::move(pending->second.callback);
            pendingDownloads_.erase(pending);
        }
        trackedDownloads_.erase(key);
        pendingCallback = nullptr;

        // CEF puede cancelar antes de que Qt responda; solicita retirar también el diálogo de ese ID.
        // Los registros ya se eliminaron, por lo que la respuesta del cierre no podrá continuar la descarga.
        if (pendingOperationId.has_value() && onRequestCloseSaveDialog_) {
            onRequestCloseSaveDialog_(*pendingOperationId);
        }
        return;
    }

    // Este callback puede llegar primero; conserva el contexto de la primera observación.
    TrackedDownload* tracked = GetOrCreateTrackedDownload(key);
    if (!tracked) {
        if (callback) {
            callback->Cancel();
        }
        return;
    }

    // Una actualización sin callback no elimina una referencia de cancelación recibida antes.
    if (callback) {
        tracked->cancelCallback = callback;
    }

    // Una cancelación ya solicitada o una interrupción no producen otro aviso de bloqueo.
    if (tracked->phase == DownloadPhase::CancelRequested || downloadItem->IsInterrupted()) {
        CancelDownload(key);
        return;
    }

    // También comunica el bloqueo si este fue el primer callback y el origen carece de contexto válido.
    // Las transferencias autorizadas no reevalúan reglas ni caducidad, pero sí conservan esta guarda de origen.
    if (!tracked->context.has_value() || !policyManager_->IsCurrentContext(*tracked->context)) {
        BlockDownload(key, SecurityReason::InvalidContext, nullptr);
    }
}

// Genera un identificador nuevo sin permitir que el contador vuelva a cero por desbordamiento.
std::optional<std::uint64_t> SecurityDownloadHandler::AllocateOperationId() {
    CEF_REQUIRE_UI_THREAD();
    if (lastOperationId_ == (std::numeric_limits<std::uint64_t>::max)()) {
        return std::nullopt;
    }

    return ++lastOperationId_;
}

// Conserva el registro de la primera observación aunque cambie el contexto en llamadas posteriores.
SecurityDownloadHandler::TrackedDownload*
SecurityDownloadHandler::GetOrCreateTrackedDownload(DownloadKey key) {
    CEF_REQUIRE_UI_THREAD();
    const auto existing = trackedDownloads_.find(key);
    if (existing != trackedDownloads_.end()) {
        return &existing->second;
    }

    const auto operationId = AllocateOperationId();
    if (!operationId.has_value()) {
        return nullptr;
    }

    // La ausencia de contexto también queda fijada; una sesión posterior no completará este origen.
    const auto state = policyManager_->ReadState();
    std::optional<SecurityContext> context;
    if (state) {
        context = state->context;
    }

    // Registrar el seguimiento no autoriza la descarga ni conserva una copia de la política.
    const auto result = trackedDownloads_.emplace(
        key, TrackedDownload{*operationId, std::move(context), {}, DownloadPhase::AwaitingAuthorization});
    return &result.first->second;
}

// Retira la continuación pendiente y solicita cancelar, conservando el seguimiento hasta el estado final.
void SecurityDownloadHandler::CancelDownload(DownloadKey key) {
    CEF_REQUIRE_UI_THREAD();

    // Mantiene la continuación localmente para actualizar el estado antes de liberar su última referencia.
    CefRefPtr<CefBeforeDownloadCallback> pendingCallback;
    // Conserva el identificador propio para cerrar únicamente el diálogo de la solicitud retirada.
    std::optional<std::uint64_t> pendingOperationId;
    const auto pending = pendingDownloads_.find(key);
    if (pending != pendingDownloads_.end()) {
        pendingOperationId = pending->second.operationId;
        pendingCallback = std::move(pending->second.callback);
        pendingDownloads_.erase(pending);
    }

    // La copia local permite llamar a CEF aunque un aviso posterior retire la entrada del mapa.
    CefRefPtr<CefDownloadItemCallback> cancelCallback;
    const auto tracked = trackedDownloads_.find(key);
    if (tracked != trackedDownloads_.end()) {
        tracked->second.phase = DownloadPhase::CancelRequested;
        cancelCallback = tracked->second.cancelCallback;
    }

    // El estado ya está preparado si liberar la continuación provoca nuevas notificaciones de CEF.
    pendingCallback = nullptr;
    if (cancelCallback) {
        cancelCallback->Cancel();
    }

    // El receptor encolará el cierre en Qt; su respuesta tardía ya no encontrará la solicitud pendiente.
    // Usa el ID copiado, sin acceder a entradas que CEF haya podido retirar durante la cancelación.
    if (pendingOperationId.has_value() && onRequestCloseSaveDialog_) {
        onRequestCloseSaveDialog_(*pendingOperationId);
    }
}

// Conserva los datos del bloqueo antes de cancelar y comunica un solo aviso por operación seguida.
void SecurityDownloadHandler::BlockDownload(
    DownloadKey key, SecurityReason reason,
    const std::shared_ptr<const PolicyManager::State>& state) {
    CEF_REQUIRE_UI_THREAD();

    // Un permiso no es un motivo de cancelación ni debe circular por el canal de avisos de bloqueo.
    const SecurityDecision decision(reason);
    if (decision.Action() != SecurityAction::Block) {
        return;
    }

    const auto tracked = trackedDownloads_.find(key);
    if (tracked == trackedDownloads_.end() || tracked->second.phase == DownloadPhase::CancelRequested) {
        // Sin registro no hay operación que correlacionar; una cancelación ya solicitada tampoco repite el aviso.
        CancelDownload(key);
        return;
    }

    // La copia permanece válida aunque CancelDownload provoque avisos CEF que eliminen el seguimiento.
    DownloadDecisionNotice notice;
    notice.operationId = tracked->second.operationId;
    notice.occurredAt = std::chrono::time_point_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now());
    notice.browserId = key.first;
    notice.downloadId = key.second;
    notice.context = tracked->second.context;
    notice.decision = decision;

    // Una lectura de otro contexto no puede atribuir su política al origen de esta descarga.
    if (state && state->policy && notice.context.has_value() && state->context == *notice.context) {
        notice.policyId = state->policy->Id();
    }

    // CancelDownload marca CancelRequested antes de llamar a CEF, impidiendo avisos duplicados por reentrada.
    CancelDownload(key);
    NotifyBlocked(notice);
}

// Separa el nombre de la carpeta sin consultar el disco ni corregir la ruta recibida.
std::optional<std::string> SecurityDownloadHandler::ExtractDestinationFileName(
    const std::string& path) {
    // Un nulo dentro de la cadena podría hacer que CEF o Windows utilizasen una ruta truncada.
    if (path.empty() || path.find('\0') != std::string::npos) {
        return std::nullopt;
    }

    try {
        // char8_t indica explícitamente UTF-8, independientemente de la página de códigos de Windows.
        const std::u8string utf8Path(path.begin(), path.end());
        const std::filesystem::path destination(utf8Path);
        if (!destination.is_absolute() || !destination.has_filename()) {
            return std::nullopt;
        }

        // Devuelve los bytes UTF-8 del nombre; DownloadPolicy comprobará su validez y sus reglas.
        const std::u8string fileName = destination.filename().u8string();
        return std::string(fileName.begin(), fileName.end());
    } catch (const std::system_error&) {
        // Un error al interpretar o convertir la ruta impide utilizarla como destino.
        return std::nullopt;
    }
}

// Resuelve el destino elegido con una lectura nueva de la política del contexto original.
void SecurityDownloadHandler::CompleteSaveDialog(DownloadKey key,
                                                 std::uint64_t operationId,
                                                 std::optional<std::string> path) {
    CEF_REQUIRE_UI_THREAD();

    // Una respuesta tardía no puede afectar a una solicitud retirada ni a otra operación con la misma clave.
    const auto pending = pendingDownloads_.find(key);
    if (pending == pendingDownloads_.end() || pending->second.operationId != operationId) {
        return;
    }
    const auto tracked = trackedDownloads_.find(key);
    if (tracked == trackedDownloads_.end() || tracked->second.operationId != operationId) {
        return;
    }

    // Cancelar voluntariamente el diálogo termina la solicitud sin evaluar ni notificar un bloqueo.
    if (!path.has_value()) {
        CancelDownload(key);
        return;
    }

    // Solo una solicitud pendiente, con continuación y contexto de origen coherentes, puede autorizarse.
    if (tracked->second.phase != DownloadPhase::AwaitingAuthorization ||
        !tracked->second.context.has_value() ||
        *tracked->second.context != pending->second.context ||
        !pending->second.callback) {
        CancelDownload(key);
        return;
    }

    // Un destino que no se puede interpretar es un fallo de destino, distinto de un bloqueo de política.
    const auto fileName = ExtractDestinationFileName(*path);
    if (!fileName.has_value()) {
        CancelDownload(key);
        return;
    }

    // La política puede haber cambiado o caducado mientras el usuario elegía el destino.
    const auto policy = policyManager_->ReadPolicy(pending->second.context);
    const SecurityDecision decision = downloadPolicy_.Evaluate(policy, *fileName);
    if (decision.Action() != SecurityAction::Allow) {
        // Conserva el motivo de la política vigente al confirmar, incluido un nombre definitivo inválido o bloqueado.
        BlockDownload(key, decision.Reason(), policy.state);
        return;
    }

    // Comprueba otra vez el origen justo antes de aplicar la decisión, sin trasladarlo a otra sesión.
    const CefString destination(*path);
    if (!policyManager_->IsCurrentContext(pending->second.context)) {
        // Un cambio de contexto después de evaluar impide aplicar el permiso y genera su propio motivo de bloqueo.
        BlockDownload(key, SecurityReason::InvalidContext, policy.state);
        return;
    }

    // Prepara los mapas antes de llamar a CEF; Continue puede provocar nuevas notificaciones.
    CefRefPtr<CefBeforeDownloadCallback> callback = std::move(pending->second.callback);
    tracked->second.phase = DownloadPhase::Authorized;
    pendingDownloads_.erase(pending);

    // Utiliza exactamente el destino evaluado y evita abrir un segundo diálogo de CEF.
    callback->Continue(destination, false);
}

// Entrega el aviso de un bloqueo aplicado sin permitir que un fallo del receptor reanude la descarga.
void SecurityDownloadHandler::NotifyBlocked(const DownloadDecisionNotice& notice) {
    CEF_REQUIRE_UI_THREAD();

    // Este canal solo comunica bloqueos, nunca descargas permitidas ni cancelaciones voluntarias.
    if (notice.decision.Action() != SecurityAction::Block) {
        return;
    }

    try {
        if (!onDecision_ || !onDecision_(notice)) {
            std::fputs("CEF download blocked; decision notice was not accepted.\n", stderr);
        }
    } catch (...) {
        // No registra mensajes de excepciones ajenas, que podrían contener rutas u otros datos de la petición.
        std::fputs("CEF download blocked; decision receiver failed.\n", stderr);
    }
}

// Solicita cancelar todas las descargas conocidas sin recorrer mapas durante las llamadas a CEF.
void SecurityDownloadHandler::CancelAll() {
    CEF_REQUIRE_UI_THREAD();

    // Copia las claves de ambos mapas; una descarga presente en los dos solo se añade una vez.
    std::set<DownloadKey> keys;
    for (const auto& entry : pendingDownloads_) {
        keys.insert(entry.first);
    }
    for (const auto& entry : trackedDownloads_) {
        keys.insert(entry.first);
    }

    // La lista independiente sigue siendo válida aunque CEF provoque la retirada de alguna entrada.
    for (const DownloadKey& key : keys) {
        CancelDownload(key);
    }
}

}  // namespace sbp
