package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Política que Spring Boot devuelve en el catálogo y al consultar la asignada a un dispositivo. */
@Serializable
internal class PolicyResponse(
    val id: Long,
    val companyId: Long,
    val name: String,
    val urlFilteringEnabled: Boolean,
    val urlFilteringMode: FilterMode,
    val urlRules: List<String>,
    val downloadControlEnabled: Boolean,
    val downloadControlMode: FilterMode,
    val downloadRules: List<String>,
    val createdAt: String,
    val updatedAt: String,
)

/** Indica si las reglas enumeran los dominios o extensiones bloqueados o permitidos. */
@Serializable
internal enum class FilterMode { DENYLIST, ALLOWLIST }
