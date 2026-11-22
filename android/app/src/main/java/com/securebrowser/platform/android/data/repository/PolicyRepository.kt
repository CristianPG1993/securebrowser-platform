package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.PolicyApi
import com.securebrowser.platform.android.data.remote.PolicyPageResponse

/** Consulta el catálogo de políticas con la sesión actual. Spring Boot comprueba el rol ADMIN. */
internal class PolicyRepository(private val api: PolicyApi, private val session: SessionStore) {
    suspend fun loadPage(page: Int, size: Int): ApiResult<PolicyPageResponse>? =
        executeAuthenticatedRequest(session) { bearer -> api.listPolicies(bearer, page, size) }
}
