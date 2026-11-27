package com.securebrowser.platform.android.ui.policies

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
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
import androidx.compose.material3.Surface
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
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
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
        item {
            Text(
                stringResource(R.string.policy_detail_title),
                style = if (ready == null) MaterialTheme.typography.headlineMedium else MaterialTheme.typography.labelLarge,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
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
                item { Text(policy.name, Modifier.semantics { heading() }, style = MaterialTheme.typography.headlineSmall) }
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
                    filterSection(R.string.policy_url_filter, R.string.policy_domains, policy.urlFilteringEnabled, policy.urlFilteringMode, policy.urlRules)
                    filterSection(R.string.policy_download_control, R.string.policy_extensions, policy.downloadControlEnabled, policy.downloadControlMode, policy.downloadRules)
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
        Text(stringResource(R.string.policy_filter_mode), style = MaterialTheme.typography.labelLarge)
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

/** Agrupa estado y modo en la cabecera. Las reglas se componen a medida que se desplaza la lista. */
private fun LazyListScope.filterSection(
    @StringRes title: Int,
    @StringRes rulesTitle: Int,
    enabled: Boolean,
    mode: FilterMode,
    rules: List<String>,
) {
    item {
        val colors = MaterialTheme.colorScheme
        Surface(
            modifier = Modifier.fillMaxWidth().padding(top = 4.dp),
            shape = MaterialTheme.shapes.medium,
            color = colors.surfaceContainer,
        ) {
            Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                FlowRow(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp),
                    itemVerticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(stringResource(title), Modifier.semantics { heading() }, style = MaterialTheme.typography.titleMedium)
                    Surface(
                        shape = CircleShape,
                        color = if (enabled) colors.primaryContainer else colors.surfaceVariant,
                        contentColor = if (enabled) colors.onPrimaryContainer else colors.onSurfaceVariant,
                    ) {
                        Text(
                            stringResource(if (enabled) R.string.policy_filter_enabled else R.string.policy_filter_disabled),
                            Modifier.padding(horizontal = 10.dp, vertical = 3.dp),
                            style = MaterialTheme.typography.labelMedium,
                        )
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(
                        stringResource(R.string.policy_filter_mode),
                        Modifier.alignByBaseline(),
                        style = MaterialTheme.typography.labelLarge,
                        color = colors.onSurfaceVariant,
                    )
                    Text(stringResource(mode.label()), Modifier.alignByBaseline(), style = MaterialTheme.typography.bodyMedium)
                }
            }
        }
    }
    item {
        Text(stringResource(rulesTitle), Modifier.padding(start = 16.dp), style = MaterialTheme.typography.labelLarge)
    }
    if (rules.isEmpty()) {
        item {
            Text(stringResource(R.string.policy_no_rules), Modifier.padding(start = 16.dp),
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
    }
    items(rules) { rule ->
        Text(rule, Modifier.padding(horizontal = 16.dp), style = MaterialTheme.typography.bodyMedium, fontFamily = FontFamily.Monospace)
    }
}
