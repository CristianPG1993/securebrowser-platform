package com.securebrowser.platform.android.ui.policies

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.PolicyPageResponse
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex

/** Estados del catálogo. Una página vacía también es una respuesta válida. */
internal sealed interface PoliciesState {
    data object Loading : PoliciesState
    class Ready(val page: PolicyPageResponse) : PoliciesState {
        val hasPrevious get() = page.page > 0
        val hasNext get() = page.page < page.totalPages - 1
    }
    data object Error : PoliciesState
    data object Forbidden : PoliciesState
    data object SessionExpired : PoliciesState
}

/** Mantiene la página solicitada y publica el estado que observará la pantalla del catálogo. */
internal class PoliciesViewModel(
    private val loadPage: suspend (page: Int, size: Int) -> ApiResult<PolicyPageResponse>?
) : ViewModel() {
    private val current = MutableStateFlow<PoliciesState>(PoliciesState.Loading)
    val state = current.asStateFlow()

    private val requestLock = Mutex()
    private var requestedPage = 0

    /** Al salir del catálogo, la siguiente visita vuelve a empezar por la primera página. */
    fun clearPage() {
        requestedPage = 0
        current.value = PoliciesState.Loading
    }

    /** Reintenta la última página solicitada, incluso si su carga falló. */
    suspend fun refresh() = requestPage(requestedPage)

    suspend fun nextPage() {
        val ready = current.value as? PoliciesState.Ready ?: return
        if (ready.hasNext) requestPage(ready.page.page + 1)
    }

    suspend fun previousPage() {
        val ready = current.value as? PoliciesState.Ready ?: return
        if (ready.hasPrevious) requestPage(ready.page.page - 1)
    }

    private suspend fun requestPage(page: Int) {
        // Una segunda pulsación mientras llega la respuesta no inicia otra consulta.
        if (!requestLock.tryLock()) return
        try {
            requestedPage = page
            current.value = PoliciesState.Loading
            val result = loadPage(page, PAGE_SIZE) ?: return
            current.value = when (result) {
                is ApiResult.Success -> PoliciesState.Ready(result.value)
                is ApiResult.HttpError -> when (result.status) {
                    401 -> PoliciesState.SessionExpired
                    403 -> PoliciesState.Forbidden
                    else -> PoliciesState.Error
                }
                ApiResult.ConnectionError, ApiResult.InvalidResponse -> PoliciesState.Error
            }
        } finally {
            requestLock.unlock()
        }
    }

    private companion object {
        const val PAGE_SIZE = 20
    }
}
