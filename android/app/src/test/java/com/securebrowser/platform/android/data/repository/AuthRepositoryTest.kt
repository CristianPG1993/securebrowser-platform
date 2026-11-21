package com.securebrowser.platform.android.data.repository

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.AuthApi
import com.securebrowser.platform.android.data.remote.LoginRequest
import com.securebrowser.platform.android.data.remote.LogoutRequest
import com.securebrowser.platform.android.data.remote.TokenResponse
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import com.securebrowser.platform.android.data.remote.UserRole
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
        check: suspend (String) -> Response<UserProfileResponse> = { error("Consulta privada inesperada") },
        reply: suspend () -> TokenResponse
    ): AuthRepository {
        val api = object : AuthApi {
            override suspend fun login(credentials: LoginRequest) = reply()
            override suspend fun logout(request: LogoutRequest) = revoke(request)
            override suspend fun checkSession(bearer: String) = check(bearer)
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

    /** Solo el rechazo de autenticación invalida el par de tokens actual. */
    @Test
    fun onlyUnauthorizedPrivateResponseClearsTokens() = runBlocking {
        var code = 200
        val repo = repository(check = { bearer ->
            assertEquals("Bearer access-test", bearer)
            when (code) {
                0 -> throw IOException("Sin conexión")
                200 -> Response.success(UserProfileResponse("Ana", "Díaz", "ana@example.invalid", UserRole.USER))
                else -> Response.error(code, "{}".toResponseBody())
            }
        }) { tokens }
        repo.login("user@example.invalid", "test")
        for (status in listOf(200, 403, 503, 0)) {
            code = status
            val result = repo.loadProfile()
            when (status) {
                0 -> assertEquals(ApiResult.ConnectionError, result)
                200 -> assertTrue(result is ApiResult.Success)
                else -> assertEquals(ApiResult.HttpError(status, null), result)
            }
            assertSame(tokens, session.currentTokens)
        }
        code = 401
        assertEquals(ApiResult.HttpError(401, null), repo.loadProfile())
        assertNull(session.currentTokens)
    }

    @Test
    fun lateUnauthorizedResponsePreservesNewSession() = runBlocking {
        val response = CompletableDeferred<Response<UserProfileResponse>>()
        val repo = repository(check = { response.await() }) { tokens }
        repo.login("first@example.invalid", "test")
        val pending = async(start = CoroutineStart.UNDISPATCHED) { repo.loadProfile() }
        val newer = TokenResponse("new-access", "Bearer", 60, "new-refresh")
        repository { newer }.login("second@example.invalid", "test")
        response.complete(Response.error(401, "{}".toResponseBody()))
        assertNull(pending.await())
        assertSame(newer, session.currentTokens)
    }

    @Test
    fun profileRequiresABodyAndReturnsTheDecodedUser() = runBlocking {
        val profile = UserProfileResponse("Ana", "Díaz", "ana@example.invalid", UserRole.ADMIN)
        var body: UserProfileResponse? = profile
        val repo = repository(check = { Response.success(body) }) { tokens }
        repo.login("user@example.invalid", "test")
        assertSame(profile, (repo.loadProfile() as ApiResult.Success).value)
        body = null
        assertEquals(ApiResult.InvalidResponse, repo.loadProfile())
        assertSame(tokens, session.currentTokens)
    }

    @Test
    fun profileArrivingAfterLogoutIsDiscarded() = runBlocking {
        val response = CompletableDeferred<Response<UserProfileResponse>>()
        val repo = repository(check = { response.await() }) { tokens }
        repo.login("user@example.invalid", "test")
        val pending = async(start = CoroutineStart.UNDISPATCHED) { repo.loadProfile() }
        session.clear()
        response.complete(Response.success(UserProfileResponse("Ana", "Díaz", "ana@example.invalid", UserRole.ADMIN)))
        assertNull(pending.await())
    }
}
