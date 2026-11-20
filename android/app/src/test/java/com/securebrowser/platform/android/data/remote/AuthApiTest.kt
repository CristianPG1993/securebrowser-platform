package com.securebrowser.platform.android.data.remote

import com.securebrowser.platform.android.core.network.createRetrofit
import kotlinx.coroutines.runBlocking
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Protocol
import okhttp3.Response
import okhttp3.ResponseBody.Companion.toResponseBody
import org.junit.Assert.assertEquals
import org.junit.Test

/** Comprueba la petición de login que Retrofit construye a partir de las anotaciones de AuthApi. */
class AuthApiTest {
    @Test
    fun loginPostsToTheVersionedApiPath() {
        // El interceptor responde en memoria y permite inspeccionar la petición antes de salir a la red.
        val client = OkHttpClient.Builder().addInterceptor { chain ->
            val request = chain.request()
            val json = """{"accessToken":"a","tokenType":"Bearer","expiresIn":900,"refreshToken":"r"}"""
            if (request.method != "POST" || request.url.encodedPath != "/api/v1/auth/login") {
                throw java.io.IOException("Ruta o método de login inesperados: ${request.method} ${request.url.encodedPath}")
            }
            Response.Builder()
                .request(request)
                .protocol(Protocol.HTTP_1_1)
                .code(200)
                .message("OK")
                .body(json.toResponseBody("application/json".toMediaType()))
                .build()
        }.build()

        val retrofit = createRetrofit("https://example.invalid/api/v1/").newBuilder()
            .client(client)
            .build()
        val api = retrofit.create(AuthApi::class.java)
        val tokens = runBlocking { api.login(LoginRequest("user@example.invalid", "demo")) }
        assertEquals("a", tokens.accessToken)
    }
}
