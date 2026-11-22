package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.DeviceApi
import com.securebrowser.platform.android.data.remote.DevicePageResponse

/** Recupera una página de dispositivos con la sesión actual. Spring Boot aplica el ámbito de cada rol. */
internal class DeviceRepository(private val api: DeviceApi, private val session: SessionStore) {
    suspend fun loadPage(page: Int, size: Int): ApiResult<DevicePageResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.listDevices(bearer, page, size) }
}
