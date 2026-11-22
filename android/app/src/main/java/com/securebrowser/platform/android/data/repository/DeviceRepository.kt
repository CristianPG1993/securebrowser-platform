package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.DeviceApi
import com.securebrowser.platform.android.data.remote.DevicePageResponse
import com.securebrowser.platform.android.data.remote.PolicyResponse

/** Consulta los dispositivos y sus políticas con la sesión actual. Spring Boot comprueba los permisos. */
internal class DeviceRepository(private val api: DeviceApi, private val session: SessionStore) {
    suspend fun loadPage(page: Int, size: Int): ApiResult<DevicePageResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.listDevices(bearer, page, size) }

    /** Consulta la política del dispositivo elegido. Una respuesta correcta actualiza su última comunicación. */
    suspend fun loadAssignedPolicy(deviceId: Long): ApiResult<PolicyResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.getAssignedPolicy(bearer, deviceId) }
}
