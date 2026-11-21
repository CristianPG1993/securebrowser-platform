package com.securebrowser.platform.android

import com.securebrowser.platform.android.data.repository.AuthRepository

/** Permite que la Activity obtenga el repositorio configurado por la variante de la app. */
internal interface AuthRepositoryProvider {
    val authRepository: AuthRepository
}
