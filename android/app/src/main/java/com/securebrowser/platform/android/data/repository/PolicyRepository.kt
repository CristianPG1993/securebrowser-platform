package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.PolicyApi
import com.securebrowser.platform.android.data.remote.PolicyPageResponse
import com.securebrowser.platform.android.data.remote.PolicyResponse

/** Consulta el catálogo de políticas con la sesión actual. Spring Boot comprueba el rol ADMIN. */
internal class PolicyRepository(private val api: PolicyApi, private val session: SessionStore) {
    suspend fun loadPage(page: Int, size: Int): ApiResult<PolicyPageResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.listPolicies(bearer, page, size) }

    /** Carga el detalle usando el ID de la política seleccionada en el catálogo. */
    suspend fun loadPolicy(id: Long): ApiResult<PolicyResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.getPolicy(bearer, id) }
}
