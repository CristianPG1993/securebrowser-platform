package com.securebrowser.platform.android.core.network

import kotlinx.serialization.json.Json
import okhttp3.MediaType.Companion.toMediaType
import retrofit2.Retrofit
import retrofit2.converter.kotlinx.serialization.asConverterFactory

/**
 * Prepara el cliente que usará [baseUrl] para resolver las rutas de la API.
 * La petición HTTP se envía cuando la app ejecuta una operación del cliente.
 */
internal fun createRetrofit(baseUrl: String): Retrofit {
    // Si el backend añade campos al JSON, la app sigue leyendo los que conoce.
    val json = Json { ignoreUnknownKeys = true }
    return Retrofit.Builder()
        .baseUrl(baseUrl)
        // El conversor escribe los cuerpos de petición como application/json y lee las respuestas JSON.
        .addConverterFactory(json.asConverterFactory("application/json".toMediaType()))
        .build()
}
