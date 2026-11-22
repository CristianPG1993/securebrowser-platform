package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Página de instalaciones visibles que devuelve GET /devices. La primera página es la 0. */
@Serializable
internal class DevicePageResponse(
    val items: List<DeviceResponse>,
    val page: Int,
    val size: Int,
    val totalElements: Long,
    val totalPages: Int,
)

/** Datos del listado. Una instalación puede carecer de nombre o de última comunicación. */
@Serializable
internal class DeviceResponse(
    val id: Long,
    val deviceIdentifier: String,
    val name: String? = null,
    val active: Boolean,
    val lastSeenAt: String? = null,
)
