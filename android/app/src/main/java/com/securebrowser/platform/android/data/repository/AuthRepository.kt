package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.network.executeApiCall
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.AuthApi
import com.securebrowser.platform.android.data.remote.LoginRequest
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** Usa [api] para enviar las credenciales y [session] para guardar en memoria los tokens recibidos. */
internal class AuthRepository(private val api: AuthApi, private val session: SessionStore) {
    /** Permite recuperar el estado de acceso al recrear la pantalla dentro del mismo proceso. */
    val hasSession: Boolean get() = session.currentTokens != null

    /** Devuelve al formulario el resultado de la operación y mantiene los tokens dentro de la capa de datos. */
    suspend fun login(email: String, password: String): ApiResult<Unit> {
        // Cada intento recibe un identificador para poder descartar respuestas que lleguen tarde.
        val attempt = session.beginLogin()

        // La corrutina espera la respuesta del backend. executeApiCall recoge el éxito o el error.
        val response = executeApiCall { api.login(LoginRequest(email, password)) }

        // Si se canceló la corrutina durante la espera, salimos antes de actualizar la sesión.
        currentCoroutineContext().ensureActive()

        // En caso de éxito, value contiene el TokenResponse. takeIf conserva ese objeto
        // si ambos tokens tienen contenido, el tipo es Bearer y la duración es positiva.
        // Si hubo un error o falla alguna condición, tokens queda en null.
        val tokens = (response as? ApiResult.Success)?.value?.takeIf {
            it.accessToken.isNotBlank() && it.refreshToken.isNotBlank() &&
                it.tokenType.equals("Bearer", ignoreCase = true) && it.expiresIn > 0
        }
        // Aunque la corrutina siga activa, otro login o un borrado de sesión puede haber
        // invalidado este intento. Solo el intento vigente puede guardar sus tokens.
        session.finishLogin(attempt, tokens)

        // El formulario recibe una confirmación (Unit) o el error correspondiente.
        // Los tokens se quedan en la sesión para las siguientes peticiones.
        return when (response) {
            is ApiResult.Success -> if (tokens != null) ApiResult.Success(Unit) else ApiResult.InvalidResponse
            is ApiResult.HttpError -> response
            ApiResult.ConnectionError -> ApiResult.ConnectionError
            ApiResult.InvalidResponse -> ApiResult.InvalidResponse
        }
    }
}
