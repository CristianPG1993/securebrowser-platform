package com.securebrowser.platform.android.ui.policies

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.data.remote.FilterMode

/** Muestra la configuración de la política seleccionada y delega las acciones en la ruta. */
@Composable
internal fun PolicyDetailScreen(
    state: PolicyDetailState,
    onRetry: () -> Unit,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    modifier: Modifier = Modifier,
    @StringRes backLabel: Int = R.string.policy_back_to_catalog
) {
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
                item { Text(policy.name, style = MaterialTheme.typography.headlineSmall) }
                filterSection(R.string.policy_url_filter, policy.urlFilteringEnabled, policy.urlFilteringMode, policy.urlRules)
                filterSection(R.string.policy_download_control, policy.downloadControlEnabled, policy.downloadControlMode, policy.downloadRules)
            }
        }
        if (state != PolicyDetailState.SessionExpired) {
            item { OutlinedButton(onClick = onBack) { Text(stringResource(backLabel)) } }
            item { OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) } }
        }
    }
}

/** Separa el estado del filtro de su configuración, que también puede consultarse si está desactivado. */
private fun LazyListScope.filterSection(@StringRes title: Int, enabled: Boolean, mode: FilterMode, rules: List<String>) {
    item { Text(stringResource(title), style = MaterialTheme.typography.titleMedium) }
    item { Text(stringResource(if (enabled) R.string.policy_filter_enabled else R.string.policy_filter_disabled)) }
    val modeLabel = when (mode) {
        FilterMode.ALLOWLIST -> R.string.policy_mode_allowlist
        FilterMode.DENYLIST -> R.string.policy_mode_denylist
    }
    item { Text(stringResource(modeLabel)) }
    if (rules.isEmpty()) item { Text(stringResource(R.string.policy_no_rules)) }
    items(rules) { rule -> Text(rule) }
}
