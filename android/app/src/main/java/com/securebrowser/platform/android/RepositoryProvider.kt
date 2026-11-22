package com.securebrowser.platform.android

import com.securebrowser.platform.android.data.repository.AuthRepository
import com.securebrowser.platform.android.data.repository.DeviceRepository

/** Expone a la Activity los repositorios configurados por la variante de la app. */
internal interface RepositoryProvider {
    val authRepository: AuthRepository
    val deviceRepository: DeviceRepository
}
