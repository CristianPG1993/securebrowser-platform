package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.SecurityEventApi
import com.securebrowser.platform.android.data.remote.SecurityEventPageResponse
import com.securebrowser.platform.android.data.remote.SecurityEventType

/** Consulta una página del historial con la sesión actual y el filtro de tipo elegido. */
internal class SecurityEventRepository(private val api: SecurityEventApi, private val session: SessionStore) {
    suspend fun loadPage(
        page: Int,
        size: Int,
        type: SecurityEventType? = null,
    ): ApiResult<SecurityEventPageResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.listEvents(bearer, page, size, type) }
}
