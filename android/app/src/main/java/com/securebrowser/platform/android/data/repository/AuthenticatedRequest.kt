package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.network.executeApiCall
import com.securebrowser.platform.android.core.session.SessionStore
import kotlinx.serialization.SerializationException
import retrofit2.HttpException
import retrofit2.Response

/** Lee el cuerpo de una consulta privada y descarta respuestas de una sesión anterior. */
internal suspend fun <T : Any> executeAuthenticatedRequest(
    session: SessionStore,
    request: suspend (bearer: String) -> Response<T>
): ApiResult<T>? {
    val tokens = session.currentTokens ?: return ApiResult.HttpError(401, null)
    val result = executeApiCall {
        val response = request("Bearer ${tokens.accessToken}")
        if (!response.isSuccessful) throw HttpException(response)
        response.body() ?: throw SerializationException("Se esperaba un cuerpo en la respuesta")
    }
    if (result is ApiResult.HttpError && result.status == 401) {
        return if (session.clearIfCurrent(tokens)) result else null
    }
    return result.takeIf { session.currentTokens === tokens }
}
