package com.securebrowser.platform.android.data.remote

import retrofit2.Response
import retrofit2.http.Body
import retrofit2.http.POST

/** Describe las peticiones de autenticación que Retrofit puede enviar al backend. */
internal interface AuthApi {
    /** Envía las credenciales como JSON y devuelve los tokens si el login tiene éxito. */
    @POST("auth/login")
    suspend fun login(@Body credentials: LoginRequest): TokenResponse

    /** El backend confirma la revocación con un 204 sin cuerpo. */
    @POST("auth/logout")
    suspend fun logout(@Body request: LogoutRequest): Response<Unit>
}
