package com.securebrowser.platform.android.data.remote

import retrofit2.Response
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.Query

/** Consulta el historial visible para la cuenta. Si type es null, Retrofit omite el filtro. */
internal interface SecurityEventApi {
    @GET("security-events")
    suspend fun listEvents(
        @Header("Authorization") bearer: String,
        @Query("page") page: Int,
        @Query("size") size: Int,
        @Query("type") type: SecurityEventType? = null,
    ): Response<SecurityEventPageResponse>
}
