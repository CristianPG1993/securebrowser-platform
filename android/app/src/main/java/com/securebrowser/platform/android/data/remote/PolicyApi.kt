package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable
import retrofit2.Response
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.Query

/** Consulta el catálogo de políticas de la empresa con la sesión de un administrador. */
internal interface PolicyApi {
    @GET("policies")
    suspend fun listPolicies(
        @Header("Authorization") bearer: String,
        @Query("page") page: Int,
        @Query("size") size: Int,
    ): Response<PolicyPageResponse>
}

/** Contiene las políticas de una página y los datos para navegar por el catálogo. */
@Serializable
internal class PolicyPageResponse(
    val items: List<PolicyResponse>,
    val page: Int,
    val size: Int,
    val totalElements: Long,
    val totalPages: Int,
)
