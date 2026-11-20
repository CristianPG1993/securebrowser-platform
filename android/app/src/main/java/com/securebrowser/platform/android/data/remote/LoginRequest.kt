package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Credenciales que Retrofit envía en el cuerpo JSON de `POST /auth/login`. */
@Serializable
internal class LoginRequest(val email: String, val password: String)
