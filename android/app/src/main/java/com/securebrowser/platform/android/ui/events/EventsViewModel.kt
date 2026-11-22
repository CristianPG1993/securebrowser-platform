package com.securebrowser.platform.android.ui.events

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.SecurityEventPageResponse
import com.securebrowser.platform.android.data.remote.SecurityEventType
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Conserva la página y el filtro seleccionados también durante la carga o tras un error. */
internal data class EventsState(
    val type: SecurityEventType? = null,
    val page: Int = 0,
    val content: EventsContent = EventsContent.Loading,
)

/** Una página sin elementos es una respuesta correcta del historial. */
internal sealed interface EventsContent {
    data object Loading : EventsContent
    class Ready(val page: SecurityEventPageResponse) : EventsContent
    data object Error : EventsContent
    data object Forbidden : EventsContent
    data object SessionExpired : EventsContent
}

/** Coordina la consulta del historial según la página y el tipo elegidos en la pantalla. */
internal class EventsViewModel(
    private val loadPage: suspend (Int, Int, SecurityEventType?) -> ApiResult<SecurityEventPageResponse>?,
) : ViewModel() {
    private val current = MutableStateFlow(EventsState())
    val state = current.asStateFlow()
    private val requestLock = Mutex()
    private var requestVersion = 0L

    fun clearPage() {
        requestVersion++
        current.value = EventsState()
    }

    suspend fun refresh() = requestPage(current.value.page, current.value.type)

    suspend fun selectType(type: SecurityEventType?) {
        if (type != current.value.type) requestPage(0, type)
    }

    suspend fun nextPage() {
        val page = (current.value.content as? EventsContent.Ready)?.page ?: return
        if (page.page < page.totalPages - 1) requestPage(page.page + 1, current.value.type)
    }

    suspend fun previousPage() {
        val page = (current.value.content as? EventsContent.Ready)?.page ?: return
        if (page.page > 0) requestPage(page.page - 1, current.value.type)
    }

    private suspend fun requestPage(page: Int, type: SecurityEventType?) {
        val version = ++requestVersion
        current.value = EventsState(type, page)
        // Si cambia el filtro durante una consulta, solo se publica la última selección.
        requestLock.withLock {
            if (version != requestVersion) return
            val result = loadPage(page, PAGE_SIZE, type) ?: return
            if (version != requestVersion) return
            val content = when (result) {
                is ApiResult.Success -> EventsContent.Ready(result.value)
                is ApiResult.HttpError -> when (result.status) {
                    401 -> EventsContent.SessionExpired
                    403 -> EventsContent.Forbidden
                    else -> EventsContent.Error
                }
                ApiResult.ConnectionError, ApiResult.InvalidResponse -> EventsContent.Error
            }
            current.value = EventsState(type, page, content)
        }
    }

    private companion object {
        const val PAGE_SIZE = 20
    }
}
