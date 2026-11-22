package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Página del historial visible para la cuenta que consulta GET /security-events. */
@Serializable
internal class SecurityEventPageResponse(
    val items: List<SecurityEventResponse>,
    val page: Int,
    val size: Int,
    val totalElements: Long,
    val totalPages: Int,
)

/** Datos que necesita el listado. occurredAt es el momento en que ocurrió el evento. */
@Serializable
internal class SecurityEventResponse(
    val id: Long,
    val deviceId: Long,
    val type: SecurityEventType,
    val occurredAt: String,
)

/** Tipos que Spring Boot devuelve en el historial y acepta como filtro de consulta. */
@Serializable
internal enum class SecurityEventType {
    URL_BLOCKED,
    DOWNLOAD_BLOCKED,
    POLICY_UPDATED,
}
