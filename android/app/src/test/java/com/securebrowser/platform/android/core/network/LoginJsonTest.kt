package com.securebrowser.platform.android.core.network

import com.securebrowser.platform.android.data.remote.LoginRequest
import com.securebrowser.platform.android.data.remote.TokenResponse
import kotlinx.serialization.json.Json
import okhttp3.ResponseBody.Companion.toResponseBody
import okio.Buffer
import org.junit.Assert.assertEquals
import org.junit.Test

/** Comprueba en memoria que el conversor de la app respeta el formato JSON del login. */
class LoginJsonTest {
    private val retrofit = createRetrofit("https://example.invalid/")

    /** Verifica el tipo de contenido y que ambos campos conservan sus valores, incluidos los espacios. */
    @Test
    fun encodesLoginCredentialsAsJson() {
        val converter = retrofit.requestBodyConverter<LoginRequest>(
            LoginRequest::class.java, emptyArray(), emptyArray()
        )
        val body = requireNotNull(converter.convert(LoginRequest("user@example.invalid", " demo ")))
        // El buffer recoge los mismos bytes que se enviarían como cuerpo de la petición.
        val buffer = Buffer()
        body.writeTo(buffer)

        val contentType = requireNotNull(body.contentType())
        assertEquals("application", contentType.type)
        assertEquals("json", contentType.subtype)
        // Comparar el JSON ya interpretado permite que los campos aparezcan en cualquier orden.
        assertEquals(
            Json.parseToJsonElement("""{"email":"user@example.invalid","password":" demo "}"""),
            Json.parseToJsonElement(buffer.readUtf8())
        )
    }

    /** Lee los tokens de una respuesta ficticia que también contiene un campo desconocido. */
    @Test
    fun decodesTokenResponseWithAnAdditionalField() {
        val converter = retrofit.responseBodyConverter<TokenResponse>(TokenResponse::class.java, emptyArray())
        val body = """{"accessToken":"a","tokenType":"Bearer","expiresIn":900,"refreshToken":"r","extra":1}"""
        val tokens = requireNotNull(converter.convert(body.toResponseBody()))

        assertEquals("a", tokens.accessToken)
        assertEquals("Bearer", tokens.tokenType)
        assertEquals(900L, tokens.expiresIn)
        assertEquals("r", tokens.refreshToken)
    }
}
