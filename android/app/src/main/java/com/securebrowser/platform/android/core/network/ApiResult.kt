package com.securebrowser.platform.android.core.network

import java.io.IOException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.serialization.Serializable
import kotlinx.serialization.SerializationException
import retrofit2.HttpException

/** Resultados que puede recibir la app al ejecutar una petición a la API. */
internal sealed interface ApiResult<out T> {
    /** Contiene los datos devueltos y decodificados correctamente. */
    class Success<T>(val value: T) : ApiResult<T>

    /** Conserva el estado HTTP aunque el servidor no incluya un código de Problem Details. */
    data class HttpError(val status: Int, val code: String?) : ApiResult<Nothing>

    /** La conexión falló antes de recibir una respuesta completa. */
    data object ConnectionError : ApiResult<Nothing>

    /** El servidor respondió con éxito, pero el JSON no encaja con el modelo esperado. */
    data object InvalidResponse : ApiResult<Nothing>
}

/** Campo del documento Problem Details que permite reconocer errores como INVALID_CREDENTIALS. */
@Serializable
private class ProblemDetails(val code: String? = null)

/** Ejecuta una operación Retrofit y deja que la cancelación se propague a la corrutina que la inició. */
internal suspend fun <T> executeApiCall(call: suspend () -> T): ApiResult<T> = try {
    ApiResult.Success(call())
} catch (error: HttpException) {
    // Una respuesta vacía o no estructurada sigue teniendo un estado HTTP válido.
    val code = try {
        error.response()?.errorBody()?.use { apiJson.decodeFromString<ProblemDetails>(it.string()).code }
    } catch (_: SerializationException) {
        null
    }
    ApiResult.HttpError(error.code(), code)
} catch (_: IOException) {
    currentCoroutineContext().ensureActive()
    ApiResult.ConnectionError
} catch (_: SerializationException) {
    ApiResult.InvalidResponse
}
