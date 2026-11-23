package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable
import retrofit2.Response
import retrofit2.http.Body
import retrofit2.http.GET
import retrofit2.http.Header
import retrofit2.http.PATCH
import retrofit2.http.Path
import retrofit2.http.Query

/** Consulta y edita las políticas de la empresa con la sesión de un administrador. */
internal interface PolicyApi {
    @GET("policies")
    suspend fun listPolicies(
        @Header("Authorization") bearer: String,
        @Query("page") page: Int,
        @Query("size") size: Int,
    ): Response<PolicyPageResponse>

    /** Obtiene el detalle de la política seleccionada en el catálogo. */
    @GET("policies/{id}")
    suspend fun getPolicy(
        @Header("Authorization") bearer: String,
        @Path("id") id: Long,
    ): Response<PolicyResponse>

    /** Envía los cambios de la política seleccionada y recibe la política actualizada. */
    @PATCH("policies/{id}")
    suspend fun updatePolicy(
        @Header("Authorization") bearer: String,
        @Path("id") id: Long,
        @Body changes: PolicyUpdateRequest,
    ): Response<PolicyResponse>
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
