package com.securebrowser.platform.android.data.remote

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.network.createRetrofit
import com.securebrowser.platform.android.core.network.executeApiCall
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Verifica el contrato HTTP del login con un servidor local exclusivo de la prueba. */
class AuthApiTest {
    // JUnit gestiona el ciclo de vida del servidor y libera su puerto al terminar la prueba.
    @get:Rule
    val server = MockWebServer()

    /** Ejecuta el mismo login de prueba con el tratamiento común de errores de la app. */
    private fun login(baseUrl: String = server.url("/api/v1/").toString()) = runBlocking {
        val api = createRetrofit(baseUrl).create(AuthApi::class.java)
        executeApiCall { api.login(LoginRequest("user@example.invalid", "demo")) }
    }

    /** Lee el código de Problem Details y el estado de la respuesta HTTP real. */
    @Test
    fun loginReadsInvalidCredentialsFromProblemDetails() {
        server.enqueue(MockResponse().setResponseCode(401)
            .setHeader("Content-Type", "application/problem+json")
            .setBody("""{"title":"Unauthorized","status":401,"code":"INVALID_CREDENTIALS"}"""))
        assertEquals(ApiResult.HttpError(401, "INVALID_CREDENTIALS"), login())
    }

    /** Un error HTTP con un cuerpo no interpretable conserva su estado. */
    @Test
    fun loginRetainsStatusWithoutProblemDetails() {
        server.enqueue(MockResponse().setResponseCode(503).setBody("Service unavailable"))
        assertEquals(ApiResult.HttpError(503, null), login())
    }

    /** Al cerrar el servidor antes del login, el cliente obtiene un fallo de transporte. */
    @Test
    fun loginReportsConnectionErrorWhenServerIsStopped() {
        val baseUrl = server.url("/api/v1/").toString()
        server.shutdown()
        assertEquals(ApiResult.ConnectionError, login(baseUrl))
    }

    /** Un JSON de éxito incompleto se distingue de un fallo de conexión. */
    @Test
    fun loginReportsInvalidResponseWhenTokensAreMissing() {
        server.enqueue(MockResponse().setResponseCode(200).setBody("{}"))
        assertEquals(ApiResult.InvalidResponse, login())
    }

    /** Comprueba la petición recibida por el servidor y los tokens que Retrofit entrega a la app. */
    @Test
    fun loginPostsToTheVersionedApiPath() {
        server.enqueue(
            MockResponse()
                .setResponseCode(200)
                .setHeader("Content-Type", "application/json")
                .setBody("""{"accessToken":"a","tokenType":"Bearer","expiresIn":900,"refreshToken":"r"}""")
        )
        val result = login()
        val tokens = (result as ApiResult.Success).value

        val request = requireNotNull(server.takeRequest(1, TimeUnit.SECONDS))
        assertEquals("POST", request.method)
        assertEquals("/api/v1/auth/login", request.path)
        assertEquals("application/json", request.getHeader("Content-Type")?.substringBefore(';'))
        assertEquals(
            Json.parseToJsonElement("""{"email":"user@example.invalid","password":"demo"}"""),
            Json.parseToJsonElement(request.body.readUtf8())
        )
        assertEquals("a", tokens.accessToken)
        assertEquals("Bearer", tokens.tokenType)
        assertEquals(900L, tokens.expiresIn)
        assertEquals("r", tokens.refreshToken)
    }
}
