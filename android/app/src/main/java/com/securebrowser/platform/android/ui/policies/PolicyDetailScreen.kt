package com.securebrowser.platform.android.ui.policies

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.dp
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.data.remote.FilterMode
import com.securebrowser.platform.android.data.remote.PolicyResponse

/** Muestra la configuración de la política seleccionada y delega las acciones en la ruta. */
@Composable
internal fun PolicyDetailScreen(
    state: PolicyDetailState,
    onRetry: () -> Unit,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    modifier: Modifier = Modifier,
    @StringRes backLabel: Int = R.string.policy_back_to_catalog,
    onSave: ((PolicyEditDraft) -> Unit)? = null,
) {
    var original by remember { mutableStateOf<PolicyResponse?>(null) }
    var draft by remember(original) { mutableStateOf(original?.let(PolicyEditDraft::from)) }
    var confirmClear by remember(draft) { mutableStateOf(false) }
    val ready = state as? PolicyDetailState.Ready
    val status = ready?.saveStatus
    val busy = status == PolicySaveStatus.Saving || status == PolicySaveStatus.Confirming
    // El GET confirmado cierra el editor. Una versión nueva tampoco debe recibir un borrador anterior.
    LaunchedEffect(ready) {
        if (ready != null && (status == PolicySaveStatus.Confirmed ||
                original?.id != ready.policy.id || original?.updatedAt != ready.policy.updatedAt)) {
            original = null
        }
    }
    BackHandler(enabled = original != null && !busy && status != PolicySaveStatus.ConfirmationFailed) { original = null }
    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item { Text(stringResource(R.string.policy_detail_title), style = MaterialTheme.typography.headlineMedium) }
        when (state) {
            PolicyDetailState.Loading -> item { CircularProgressIndicator() }
            PolicyDetailState.Forbidden -> item { Text(stringResource(R.string.policy_detail_forbidden)) }
            PolicyDetailState.NotFound -> item { Text(stringResource(R.string.policy_detail_not_found)) }
            PolicyDetailState.DeviceInactive -> item { Text(stringResource(R.string.policy_detail_device_inactive)) }
            PolicyDetailState.SessionExpired -> item { Text(stringResource(R.string.login_session_expired)) }
            PolicyDetailState.Error -> {
                item { Text(stringResource(R.string.policy_detail_error)) }
                item { Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) } }
            }
            is PolicyDetailState.Ready -> {
                val policy = state.policy
                val editing = draft.takeIf { original?.id == policy.id && original?.updatedAt == policy.updatedAt }
                item { Text(policy.name, style = MaterialTheme.typography.headlineSmall) }
                if (busy) {
                    item { CircularProgressIndicator() }
                    item { Text(stringResource(if (status == PolicySaveStatus.Saving) R.string.policy_saving else R.string.policy_confirming)) }
                } else if (status == PolicySaveStatus.ConfirmationFailed) {
                    item { Text(stringResource(R.string.policy_confirmation_failed)) }
                    item { Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) } }
                } else if (onSave != null && editing != null) {
                    item {
                        FilterControls(
                            title = R.string.policy_url_filter,
                            enabled = editing.urlFilteringEnabled,
                            mode = editing.urlFilteringMode,
                            onEnabledChange = { draft = editing.copy(urlFilteringEnabled = it) },
                            onModeChange = { draft = editing.copy(urlFilteringMode = it) },
                        )
                    }
                    item {
                        OutlinedTextField(
                            value = editing.urlRulesText,
                            onValueChange = { draft = editing.copy(urlRulesText = it) },
                            label = { Text(stringResource(R.string.policy_edit_domains)) },
                            minLines = 2,
                        )
                    }
                    item {
                        FilterControls(
                            title = R.string.policy_download_control,
                            enabled = editing.downloadControlEnabled,
                            mode = editing.downloadControlMode,
                            onEnabledChange = { draft = editing.copy(downloadControlEnabled = it) },
                            onModeChange = { draft = editing.copy(downloadControlMode = it) },
                        )
                    }
                    item {
                        OutlinedTextField(
                            value = editing.downloadRulesText,
                            onValueChange = { draft = editing.copy(downloadRulesText = it) },
                            label = { Text(stringResource(R.string.policy_edit_extensions)) },
                            minLines = 2,
                        )
                    }
                    item {
                        val changes = editing.toUpdateRequest(policy)
                        if (status == PolicySaveStatus.InvalidInput || status == PolicySaveStatus.SaveFailed) {
                            Text(stringResource(if (status == PolicySaveStatus.InvalidInput) R.string.policy_invalid_rules else R.string.policy_save_failed))
                        }
                        Button(enabled = changes != null, onClick = {
                            if (changes?.urlRules?.isEmpty() == true || changes?.downloadRules?.isEmpty() == true) {
                                confirmClear = true
                            } else {
                                onSave(editing)
                            }
                        }) { Text(stringResource(R.string.policy_save)) }
                        if (confirmClear) {
                            AlertDialog(
                                onDismissRequest = { confirmClear = false },
                                text = { Text(stringResource(R.string.policy_clear_rules_question)) },
                                confirmButton = {
                                    Button(onClick = {
                                        confirmClear = false
                                        onSave(editing)
                                    }) { Text(stringResource(R.string.policy_save)) }
                                },
                                dismissButton = {
                                    OutlinedButton(onClick = { confirmClear = false }) { Text(stringResource(R.string.policy_cancel)) }
                                },
                            )
                        }
                    }
                    item { OutlinedButton(onClick = { original = null }) { Text(stringResource(R.string.policy_cancel)) } }
                } else {
                    if (status == PolicySaveStatus.Confirmed) item { Text(stringResource(R.string.policy_saved)) }
                    if (onSave != null) {
                        item {
                            Button(onClick = { original = policy }) {
                                Text(stringResource(R.string.policy_edit))
                            }
                        }
                    }
                    filterSection(R.string.policy_url_filter, policy.urlFilteringEnabled, policy.urlFilteringMode, policy.urlRules)
                    filterSection(R.string.policy_download_control, policy.downloadControlEnabled, policy.downloadControlMode, policy.downloadRules)
                }
            }
        }
        if (state != PolicyDetailState.SessionExpired) {
            item { OutlinedButton(onClick = onBack) { Text(stringResource(backLabel)) } }
            item { OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) } }
        }
    }
}

/** Edita la activación y el modo de uno de los filtros del borrador. */
@Composable
private fun FilterControls(
    @StringRes title: Int,
    enabled: Boolean,
    mode: FilterMode,
    onEnabledChange: (Boolean) -> Unit,
    onModeChange: (FilterMode) -> Unit,
) {
    Column {
        Row(
            modifier = Modifier.fillMaxWidth().toggleable(value = enabled, role = Role.Switch, onValueChange = onEnabledChange),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(stringResource(title), style = MaterialTheme.typography.titleMedium)
            Switch(checked = enabled, onCheckedChange = null)
        }
        Column(Modifier.selectableGroup()) {
            FilterMode.entries.forEach { option ->
                Row(
                    modifier = Modifier.fillMaxWidth()
                        .selectable(selected = mode == option, role = Role.RadioButton, onClick = { onModeChange(option) })
                        .heightIn(min = 48.dp),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    RadioButton(selected = mode == option, onClick = null)
                    Text(stringResource(option.label()))
                }
            }
        }
    }
}

@StringRes
private fun FilterMode.label(): Int = when (this) {
    FilterMode.ALLOWLIST -> R.string.policy_mode_allowlist
    FilterMode.DENYLIST -> R.string.policy_mode_denylist
}

/** Separa el estado del filtro de su configuración, que también puede consultarse si está desactivado. */
private fun LazyListScope.filterSection(@StringRes title: Int, enabled: Boolean, mode: FilterMode, rules: List<String>) {
    item { Text(stringResource(title), style = MaterialTheme.typography.titleMedium) }
    item { Text(stringResource(if (enabled) R.string.policy_filter_enabled else R.string.policy_filter_disabled)) }
    item { Text(stringResource(mode.label())) }
    if (rules.isEmpty()) item { Text(stringResource(R.string.policy_no_rules)) }
    items(rules) { rule -> Text(rule) }
}
