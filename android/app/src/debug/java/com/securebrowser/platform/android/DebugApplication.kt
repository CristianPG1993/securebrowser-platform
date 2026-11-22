package com.securebrowser.platform.android

import android.app.Application
import com.securebrowser.platform.android.core.network.createRetrofit
import com.securebrowser.platform.android.core.session.SessionStore
import com.securebrowser.platform.android.data.remote.AuthApi
import com.securebrowser.platform.android.data.remote.DeviceApi
import com.securebrowser.platform.android.data.remote.PolicyApi
import com.securebrowser.platform.android.data.repository.AuthRepository
import com.securebrowser.platform.android.data.repository.DeviceRepository
import com.securebrowser.platform.android.data.repository.PolicyRepository

/** Prepara las dependencias de red del emulador al iniciar el proceso de la app de desarrollo. */
internal class DebugApplication : Application(), RepositoryProvider {
    private val retrofit = createRetrofit(BuildConfig.API_BASE_URL)
    // Los repositorios comparten la sesión que establece el login.
    private val session = SessionStore()

    override val authRepository = AuthRepository(retrofit.create(AuthApi::class.java), session)
    override val deviceRepository = DeviceRepository(retrofit.create(DeviceApi::class.java), session)
    override val policyRepository = PolicyRepository(retrofit.create(PolicyApi::class.java), session)
}
