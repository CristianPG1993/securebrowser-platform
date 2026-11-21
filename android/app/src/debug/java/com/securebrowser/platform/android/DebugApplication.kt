package com.securebrowser.platform.android

import android.app.Application
import com.securebrowser.platform.android.core.network.createRetrofit
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.AuthApi
import com.securebrowser.platform.android.data.repository.AuthRepository

/** Prepara las dependencias de red del emulador al iniciar el proceso de la app de desarrollo. */
internal class DebugApplication : Application() {
    /** Cliente de login compartido durante la vida del proceso. La petición se envía al invocar login. */
    private val authApi: AuthApi = createRetrofit(BuildConfig.API_BASE_URL).create(AuthApi::class.java)

    /** La app comparte un repositorio y una sesión en memoria mientras vive este proceso. */
    val authRepository = AuthRepository(authApi, SessionStore())
}
