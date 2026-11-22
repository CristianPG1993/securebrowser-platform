package com.securebrowser.platform.android.data.remote

import retrofit2.Response
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.Path
import retrofit2.http.Query

/** Consulta las instalaciones visibles para la cuenta que presenta el token. */
internal interface DeviceApi {
    @GET("devices")
    suspend fun listDevices(
        @Header("Authorization") bearer: String,
        @Query("page") page: Int,
        @Query("size") size: Int,
    ): Response<DevicePageResponse>

    /** Una consulta correcta de la política asignada actualiza lastSeenAt en el dispositivo. */
    @GET("devices/{id}/policy")
    suspend fun getAssignedPolicy(
        @Header("Authorization") bearer: String,
        @Path("id") deviceId: Long,
    ): Response<PolicyResponse>
}
