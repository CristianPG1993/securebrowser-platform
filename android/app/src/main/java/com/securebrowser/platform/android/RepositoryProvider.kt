package com.securebrowser.platform.android

import com.securebrowser.platform.android.data.repository.AuthRepository
import com.securebrowser.platform.android.data.repository.DeviceRepository
import com.securebrowser.platform.android.data.repository.PolicyRepository
import com.securebrowser.platform.android.data.repository.SecurityEventRepository

/** Expone a la Activity los repositorios configurados por la variante de la app. */
internal interface RepositoryProvider {
    val authRepository: AuthRepository
    val deviceRepository: DeviceRepository
    val policyRepository: PolicyRepository
    val securityEventRepository: SecurityEventRepository
}
