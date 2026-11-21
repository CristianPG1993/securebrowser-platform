package com.securebrowser.platform.android.core.session

import com.securebrowser.platform.android.data.remote.TokenResponse
import kotlinx.coroutines.CancellationException

/** Conserva el par de tokens en memoria durante la vida del proceso. */
internal class SessionStore {
    private var generation = 0L

    /** Una sola referencia permite leer ambos tokens de la misma sesión. */
    @Volatile
    var currentTokens: TokenResponse? = null
        private set

    /** Un nuevo intento de login descarta la sesión anterior y recibe su propio identificador. */
    @Synchronized
    fun beginLogin(): Long {
        clear()
        return generation
    }

    /** Acepta el resultado solo si el intento sigue vigente, incluso cuando terminó con un error. */
    @Synchronized
    fun finishLogin(attempt: Long, tokens: TokenResponse?) {
        if (attempt != generation) throw CancellationException("Intento de login descartado")
        currentTokens = tokens
    }

    /** Un 401 tardío solo puede retirar el par que se usó al enviar la petición. */
    @Synchronized
    fun clearIfCurrent(tokens: TokenResponse): Boolean {
        if (currentTokens !== tokens) return false
        clear()
        return true
    }

    /** Retira ambos tokens e invalida los logins pendientes antes de devolver el refresh para revocarlo. */
    @Synchronized
    fun clear(): String? {
        val refreshToken = currentTokens?.refreshToken
        generation++
        currentTokens = null
        return refreshToken
    }
}
