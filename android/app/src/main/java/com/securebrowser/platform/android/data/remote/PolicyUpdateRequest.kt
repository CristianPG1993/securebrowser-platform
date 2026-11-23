package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Cambios para el PATCH de una política. El conversor omite los campos con su valor por defecto. */
@Serializable
internal class PolicyUpdateRequest(
    val urlFilteringEnabled: Boolean? = null,
    val urlFilteringMode: FilterMode? = null,
    val urlRules: List<String>? = null,
    val downloadControlEnabled: Boolean? = null,
    val downloadControlMode: FilterMode? = null,
    val downloadRules: List<String>? = null,
) {
    init {
        require(
            urlFilteringEnabled != null || urlFilteringMode != null || urlRules != null ||
                downloadControlEnabled != null || downloadControlMode != null || downloadRules != null
        ) { "Policy update must include at least one field" }
    }
}
