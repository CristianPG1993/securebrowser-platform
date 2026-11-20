package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/**
 * Representa el JSON que devuelve el backend al iniciar sesión o renovar los tokens.
 *
 * @property accessToken Token que la app presenta al llamar a una ruta protegida.
 * @property tokenType Esquema de autenticación que acompaña al token de acceso, por ejemplo `Bearer`.
 * @property expiresIn Segundos de validez del token de acceso desde su emisión.
 * @property refreshToken Token que se envía al backend para solicitar un nuevo par de tokens.
 */
@Serializable
internal class TokenResponse(
    val accessToken: String,
    val tokenType: String,
    val expiresIn: Long,
    val refreshToken: String,
)
