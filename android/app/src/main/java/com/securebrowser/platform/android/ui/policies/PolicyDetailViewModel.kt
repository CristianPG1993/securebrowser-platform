package com.securebrowser.platform.android.ui.policies

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.PolicyResponse
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Estados de la consulta de una política del catálogo. */
internal sealed interface PolicyDetailState {
    data object Loading : PolicyDetailState
    class Ready(val policy: PolicyResponse) : PolicyDetailState
    data object Forbidden : PolicyDetailState
    data object NotFound : PolicyDetailState
    data object SessionExpired : PolicyDetailState
    data object Error : PolicyDetailState
}

/** Carga la política seleccionada y descarta respuestas de consultas anteriores. */
internal class PolicyDetailViewModel(
    private val loadPolicy: suspend (Long) -> ApiResult<PolicyResponse>?
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
        val version = ++requestVersion
        current.value = PolicyDetailState.Loading
        val result = loadPolicy(id) ?: return
        // El usuario puede haber salido del detalle o seleccionado otra política.
        if (version != requestVersion) return
        current.value = when (result) {
            is ApiResult.Success -> PolicyDetailState.Ready(result.value)
            is ApiResult.HttpError -> when (result.status) {
                401 -> PolicyDetailState.SessionExpired
                403 -> PolicyDetailState.Forbidden
                404 -> PolicyDetailState.NotFound
                else -> PolicyDetailState.Error
            }
            ApiResult.ConnectionError, ApiResult.InvalidResponse -> PolicyDetailState.Error
        }
    }
}
