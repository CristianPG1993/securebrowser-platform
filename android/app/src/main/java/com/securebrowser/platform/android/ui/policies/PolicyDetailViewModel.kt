package com.securebrowser.platform.android.ui.policies

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.PolicyResponse
import com.securebrowser.platform.android.data.remote.PolicyUpdateRequest
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Progreso del guardado mientras está abierto el detalle de la política. */
internal enum class PolicySaveStatus {
    Idle, Saving, InvalidInput, SaveFailed, Confirming, ConfirmationFailed, Confirmed
}

/** Estados del detalle de una política del catálogo o de un dispositivo. */
internal sealed interface PolicyDetailState {
    data object Loading : PolicyDetailState
    data class Ready(val policy: PolicyResponse, val saveStatus: PolicySaveStatus = PolicySaveStatus.Idle) : PolicyDetailState
    data object Forbidden : PolicyDetailState
    data object NotFound : PolicyDetailState
    data object DeviceInactive : PolicyDetailState
    data object SessionExpired : PolicyDetailState
    data object Error : PolicyDetailState
}

/** Carga la política seleccionada y descarta respuestas de consultas anteriores. */
internal class PolicyDetailViewModel(
    private val loadPolicy: suspend (Long) -> ApiResult<PolicyResponse>?,
    private val updatePolicy: (suspend (Long, PolicyUpdateRequest) -> ApiResult<PolicyResponse>?)? = null,
) : ViewModel() {
    private val current = MutableStateFlow<PolicyDetailState>(PolicyDetailState.Loading)
    val state = current.asStateFlow()
    private var requestVersion = 0L

    /** Al salir del detalle, también invalida cualquier petición pendiente. */
    fun clearDetail() {
        requestVersion++
        current.value = PolicyDetailState.Loading
    }

    suspend fun refresh(id: Long) {
        val ready = current.value as? PolicyDetailState.Ready
        if (ready?.policy?.id == id) {
            when (ready.saveStatus) {
                PolicySaveStatus.Saving -> return
                PolicySaveStatus.Confirming, PolicySaveStatus.ConfirmationFailed -> {
                    confirm(ready)
                    return
                }
                else -> Unit
            }
        }
        val version = ++requestVersion
        current.value = PolicyDetailState.Loading
        val result = loadPolicy(id) ?: return
        // El usuario puede haber salido del detalle o seleccionado otra política.
        if (version != requestVersion) return
        current.value = if (result is ApiResult.Success) PolicyDetailState.Ready(result.value)
            else errorState(result, PolicyDetailState.Error)
    }

    /** Envía solo los cambios del borrador. La consulta desde Dispositivos carece de acción de guardado. */
    suspend fun save(draft: PolicyEditDraft) {
        val update = updatePolicy ?: return
        val ready = current.value as? PolicyDetailState.Ready ?: return
        when (ready.saveStatus) {
            PolicySaveStatus.Saving, PolicySaveStatus.Confirming, PolicySaveStatus.ConfirmationFailed -> return
            else -> Unit
        }
        val changes = draft.toUpdateRequest(ready.policy) ?: return
        val version = ++requestVersion
        current.value = ready.copy(saveStatus = PolicySaveStatus.Saving)
        val result = update(ready.policy.id, changes) ?: return
        if (version != requestVersion) return
        if (result is ApiResult.Success) {
            confirm(ready)
            return
        }
        val status = if (result is ApiResult.HttpError && result.status == 400) PolicySaveStatus.InvalidInput
            else PolicySaveStatus.SaveFailed
        current.value = errorState(result, ready.copy(saveStatus = status))
    }

    /** Lee de nuevo el mismo ID. Si la lectura falla, refresh reintenta únicamente este GET. */
    private suspend fun confirm(ready: PolicyDetailState.Ready) {
        val version = ++requestVersion
        current.value = ready.copy(saveStatus = PolicySaveStatus.Confirming)
        val result = loadPolicy(ready.policy.id) ?: return
        if (version != requestVersion) return
        current.value = if (result is ApiResult.Success) {
            PolicyDetailState.Ready(result.value, PolicySaveStatus.Confirmed)
        } else {
            errorState(result, ready.copy(saveStatus = PolicySaveStatus.ConfirmationFailed))
        }
    }

    private fun errorState(result: ApiResult<*>, fallback: PolicyDetailState): PolicyDetailState =
        when ((result as? ApiResult.HttpError)?.status) {
            401 -> PolicyDetailState.SessionExpired
            403 -> PolicyDetailState.Forbidden
            404 -> PolicyDetailState.NotFound
            409 -> if (result.code == "DEVICE_INACTIVE") PolicyDetailState.DeviceInactive else fallback
            else -> fallback
        }
}
