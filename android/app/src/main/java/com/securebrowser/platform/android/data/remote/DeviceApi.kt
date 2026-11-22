package com.securebrowser.platform.android.data.remote

import retrofit2.Response
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.Query

/** Consulta las instalaciones visibles para la cuenta que presenta el token. */
internal interface DeviceApi {
    @GET("devices")
    suspend fun listDevices(
        @Header("Authorization") bearer: String,
        @Query("page") page: Int,
        @Query("size") size: Int,
    ): Response<DevicePageResponse>
}
