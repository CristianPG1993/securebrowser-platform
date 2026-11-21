package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.AuthApi
import com.securebrowser.platform.android.data.remote.LoginRequest
import com.securebrowser.platform.android.data.remote.LogoutRequest
import com.securebrowser.platform.android.data.remote.TokenResponse
import java.io.IOException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import retrofit2.HttpException
import retrofit2.Response

/** Comprueba qué respuesta de login puede establecer la sesión en memoria. */
class AuthRepositoryTest {
    private val session = SessionStore()
    private val tokens = TokenResponse("access-test", "Bearer", 900, "refresh-test")

    /** Sustituye solo la API remota para controlar el orden de las respuestas en estas pruebas. */
    private fun repository(
        revoke: suspend (LogoutRequest) -> Response<Unit> = { error("Logout inesperado") },
        reply: suspend () -> TokenResponse
    ): AuthRepository {
        val api = object : AuthApi {
            override suspend fun login(credentials: LoginRequest) = reply()
            override suspend fun logout(request: LogoutRequest) = revoke(request)
        }
        return AuthRepository(api, session)
    }

    /** El formulario recibe confirmación y la sesión conserva los dos tokens juntos. */
    @Test
    fun successfulLoginStoresTokensAndReturnsUnit() = runBlocking {
        val result = repository { tokens }.login("user@example.invalid", "test")
        assertEquals(Unit, (result as ApiResult.Success).value)
        assertSame(tokens, session.currentTokens)
    }

    /** El rechazo de credenciales llega al formulario y descarta la identidad anterior. */
    @Test
    fun rejectedLoginReturnsBackendCodeAndLeavesSessionEmpty() = runBlocking {
        repository { tokens }.login("first@example.invalid", "test")
        val result = repository {
            throw HttpException(Response.error<TokenResponse>(401, """{"code":"INVALID_CREDENTIALS"}""".toResponseBody()))
        }.login("second@example.invalid", "test")
        assertEquals(ApiResult.HttpError(401, "INVALID_CREDENTIALS"), result)
        assertNull(session.currentTokens)
    }

    /** Un par incompleto no establece una sesión. */
    @Test
    fun blankRefreshTokenDoesNotEstablishSession() = runBlocking {
        val result = repository { TokenResponse("access", "Bearer", 900, "") }.login("user@example.invalid", "test")
        assertEquals(ApiResult.InvalidResponse, result)
        assertNull(session.currentTokens)
    }

    /** Descartar la sesión invalida incluso un login que ya estaba esperando al backend. */
    @Test
    fun clearingSessionDiscardsLateLogin() = runBlocking {
        val delayed = CompletableDeferred<TokenResponse>()
        val pending = async(start = CoroutineStart.UNDISPATCHED) {
            repository { delayed.await() }.login("user@example.invalid", "test")
        }
        assertEquals(LogoutResult.LocalOnly, repository { tokens }.logout())
        delayed.complete(tokens)
        pending.join()
        assertTrue(pending.isCancelled)
        assertNull(session.currentTokens)
    }

    /** Una respuesta antigua tampoco puede reemplazar una sesión nueva. */
    @Test
    fun newerLoginWinsEvenWhenOlderLoginFinishesLast() = runBlocking {
        val delayed = CompletableDeferred<TokenResponse>()
        val pending = async(start = CoroutineStart.UNDISPATCHED) {
            repository { delayed.await() }.login("first@example.invalid", "test")
        }
        repository { tokens }.login("second@example.invalid", "test")
        delayed.complete(TokenResponse("old-access", "Bearer", 60, "old-refresh"))
        pending.join()
        assertTrue(pending.isCancelled)
        assertSame(tokens, session.currentTokens)
    }

    /** El cierre local es inmediato y la confirmación de la sesión anterior respeta el siguiente login. */
    @Test
    fun delayedLogoutOnlyRevokesTheOriginalSession() = runBlocking {
        val response = CompletableDeferred<Response<Unit>>()
        val repo = repository(revoke = { request ->
            assertEquals(tokens.refreshToken, request.refreshToken)
            response.await()
        }) { tokens }
        repo.login("first@example.invalid", "test")
        val closing = async(start = CoroutineStart.UNDISPATCHED) { repo.logout() }
        assertNull(session.currentTokens)
        val newer = TokenResponse("new-access", "Bearer", 900, "new-refresh")
        repository { newer }.login("second@example.invalid", "test")
        response.complete(Response.success<Unit>(204, null))
        assertEquals(LogoutResult.Revoked, closing.await())
        assertSame(newer, session.currentTokens)
    }

    @Test
    fun failedLogoutStillRemovesLocalTokens() = runBlocking {
        val repo = repository(revoke = { throw IOException("Sin conexión") }) { tokens }
        repo.login("user@example.invalid", "test")
        assertEquals(LogoutResult.LocalOnly, repo.logout())
        assertNull(session.currentTokens)
    }

    @Test
    fun serverRejectionDoesNotConfirmRevocation() = runBlocking {
        val repo = repository(revoke = { Response.error(401, "{}".toResponseBody()) }) { tokens }
        repo.login("user@example.invalid", "test")
        assertEquals(LogoutResult.LocalOnly, repo.logout())
        assertNull(session.currentTokens)
    }
}
