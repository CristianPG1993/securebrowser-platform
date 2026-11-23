package com.securebrowser.platform.android.ui.policies

import com.securebrowser.platform.android.data.remote.FilterMode
import com.securebrowser.platform.android.data.remote.PolicyResponse
import com.securebrowser.platform.android.data.remote.PolicyUpdateRequest

/** Valores del formulario mientras el administrador edita la política. */
internal data class PolicyEditDraft(
    val urlFilteringEnabled: Boolean,
    val urlFilteringMode: FilterMode,
    val urlRulesText: String,
    val downloadControlEnabled: Boolean,
    val downloadControlMode: FilterMode,
    val downloadRulesText: String,
) {
    /** Compara el formulario con la política consultada. Devuelve null si no hay cambios. */
    fun toUpdateRequest(original: PolicyResponse): PolicyUpdateRequest? {
        fun rulesFrom(text: String): List<String> =
            text.lineSequence().map { it.trim() }.filter { it.isNotEmpty() }.toList()

        val urlEnabled = urlFilteringEnabled.takeIf { it != original.urlFilteringEnabled }
        val urlMode = urlFilteringMode.takeIf { it != original.urlFilteringMode }
        val urlRules = rulesFrom(urlRulesText).takeIf { it != original.urlRules }
        val downloadEnabled = downloadControlEnabled.takeIf { it != original.downloadControlEnabled }
        val downloadMode = downloadControlMode.takeIf { it != original.downloadControlMode }
        val downloadRules = rulesFrom(downloadRulesText).takeIf { it != original.downloadRules }

        if (urlEnabled == null && urlMode == null && urlRules == null &&
            downloadEnabled == null && downloadMode == null && downloadRules == null
        ) return null

        return PolicyUpdateRequest(
            urlFilteringEnabled = urlEnabled,
            urlFilteringMode = urlMode,
            urlRules = urlRules,
            downloadControlEnabled = downloadEnabled,
            downloadControlMode = downloadMode,
            downloadRules = downloadRules,
        )
    }

    companion object {
        /** Inicializa el formulario con la política consultada y una regla por línea. */
        fun from(policy: PolicyResponse): PolicyEditDraft = PolicyEditDraft(
            urlFilteringEnabled = policy.urlFilteringEnabled,
            urlFilteringMode = policy.urlFilteringMode,
            urlRulesText = policy.urlRules.joinToString("\n"),
            downloadControlEnabled = policy.downloadControlEnabled,
            downloadControlMode = policy.downloadControlMode,
            downloadRulesText = policy.downloadRules.joinToString("\n"),
        )
    }
}
