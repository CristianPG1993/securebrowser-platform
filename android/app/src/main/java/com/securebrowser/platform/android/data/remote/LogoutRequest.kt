package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Token de renovación de la sesión que se solicita revocar. */
@Serializable
internal class LogoutRequest(val refreshToken: String)
