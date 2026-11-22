package com.securebrowser.platform.android.ui.policies

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.securebrowser.platform.android.R

/** Muestra el catálogo y comunica a la navegación el ID de la política seleccionada. */
@Composable
internal fun PoliciesScreen(
    state: PoliciesState,
    onOpenPolicy: (Long) -> Unit,
    onPrevious: () -> Unit,
    onNext: () -> Unit,
    onRetry: () -> Unit,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    modifier: Modifier = Modifier
) {
    LazyColumn(
        modifier = modifier.fillMaxSize(),
        contentPadding = PaddingValues(24.dp),
        verticalArrangement = Arrangement.spacedBy(12.dp)
    ) {
        item { Text(stringResource(R.string.policies_title), style = MaterialTheme.typography.headlineMedium) }
        when (state) {
            PoliciesState.Loading -> item { CircularProgressIndicator() }
            PoliciesState.Error -> {
                item { Text(stringResource(R.string.policies_load_error)) }
                item { Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) } }
            }
            PoliciesState.Forbidden -> item { Text(stringResource(R.string.policies_forbidden)) }
            PoliciesState.SessionExpired -> item { Text(stringResource(R.string.login_session_expired)) }
            is PoliciesState.Ready -> {
                val page = state.page
                if (page.items.isEmpty()) item { Text(stringResource(R.string.policies_empty)) }
                items(page.items, key = { it.id }) { policy ->
                    OutlinedCard(
                        onClick = { onOpenPolicy(policy.id) },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(policy.name, Modifier.padding(16.dp), style = MaterialTheme.typography.titleMedium)
                    }
                }
                if (state.hasPrevious || state.hasNext) {
                    if (page.page < page.totalPages) item {
                        Text(stringResource(R.string.devices_page, page.page + 1, page.totalPages))
                    }
                    item {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedButton(onClick = onPrevious, enabled = state.hasPrevious) {
                                Text(stringResource(R.string.devices_previous))
                            }
                            OutlinedButton(onClick = onNext, enabled = state.hasNext) {
                                Text(stringResource(R.string.devices_next))
                            }
                        }
                    }
                }
            }
        }
        if (state != PoliciesState.SessionExpired) {
            item { OutlinedButton(onClick = onBack) { Text(stringResource(R.string.dashboard_return)) } }
            item { OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) } }
        }
    }
}
