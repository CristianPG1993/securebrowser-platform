package com.securebrowser.platform.android.ui.devices

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.DevicePageResponse
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex

/** Una página vacía conserva sus metadatos para poder volver a la página anterior. */
internal sealed interface DevicesState {
    data object Loading : DevicesState
    class Ready(val page: DevicePageResponse) : DevicesState {
        val isEmpty get() = page.items.isEmpty()
        val hasPrevious get() = page.page > 0
        val hasNext get() = page.page < page.totalPages - 1
    }
    data object Error : DevicesState
    data object SessionExpired : DevicesState
}

/** Mantiene la página solicitada y publica el resultado para la pantalla de dispositivos. */
internal class DevicesViewModel(
    private val loadPage: suspend (page: Int, size: Int) -> ApiResult<DevicePageResponse>?
) : ViewModel() {
    private val current = MutableStateFlow<DevicesState>(DevicesState.Loading)
    val state = current.asStateFlow()

    private val requestLock = Mutex()
    private var requestedPage = 0

    /** Al salir del listado se descartan la página y los datos de esa visita. */
    fun clearPage() {
        requestedPage = 0
        current.value = DevicesState.Loading
    }

    /** La primera consulta empieza en cero. Un reintento reutiliza el número que falló. */
    suspend fun refresh() = requestPage(requestedPage)

    suspend fun nextPage() {
        val ready = current.value as? DevicesState.Ready ?: return
        if (ready.hasNext) requestPage(ready.page.page + 1)
    }

    suspend fun previousPage() {
        val ready = current.value as? DevicesState.Ready ?: return
        if (ready.hasPrevious) requestPage(ready.page.page - 1)
    }

    private suspend fun requestPage(page: Int) {
        // Ignora pulsaciones repetidas mientras se está consultando una página.
        if (!requestLock.tryLock()) return
        try {
            requestedPage = page
            current.value = DevicesState.Loading
            val result = loadPage(page, PAGE_SIZE) ?: return
            current.value = when (result) {
                is ApiResult.Success -> DevicesState.Ready(result.value)
                is ApiResult.HttpError -> if (result.status == 401) DevicesState.SessionExpired else DevicesState.Error
                ApiResult.ConnectionError, ApiResult.InvalidResponse -> DevicesState.Error
            }
        } finally {
            requestLock.unlock()
        }
    }

    private companion object {
        const val PAGE_SIZE = 20
    }
}
