package com.securebrowser.platform.android.ui.devices

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
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
import com.securebrowser.platform.android.data.remote.DeviceResponse

/** Dibuja la página actual y delega las acciones de navegación y carga. */
@Composable
internal fun DevicesScreen(
    state: DevicesState,
    onOpenPolicy: ((Long) -> Unit)?,
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
        item { Text(stringResource(R.string.devices_title), style = MaterialTheme.typography.headlineMedium) }
        when (state) {
            DevicesState.Loading -> item { CircularProgressIndicator() }
            DevicesState.Error -> item {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    Text(stringResource(R.string.devices_load_error))
                    Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) }
                }
            }
            DevicesState.SessionExpired -> item { Text(stringResource(R.string.login_session_expired)) }
            is DevicesState.Ready -> {
                if (state.isEmpty) item { Text(stringResource(R.string.devices_empty)) }
                items(state.page.items, key = { it.id }) { device -> DeviceCard(device, onOpenPolicy) }
                if (state.hasPrevious || state.hasNext) item {
                    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                        if (!state.isEmpty) {
                            Text(stringResource(R.string.devices_page, state.page.page + 1, state.page.totalPages))
                        }
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
        if (state != DevicesState.SessionExpired) {
            item { OutlinedButton(onClick = onBack) { Text(stringResource(R.string.dashboard_return)) } }
            item { OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) } }
        }
    }
}

/** Usa el identificador de instalación si el dispositivo carece de nombre. */
@Composable
private fun DeviceCard(device: DeviceResponse, onOpenPolicy: ((Long) -> Unit)?) {
    OutlinedCard(modifier = Modifier.fillMaxWidth()) {
        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(device.name?.takeIf { it.isNotBlank() } ?: device.deviceIdentifier,
                style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.devices_id, device.id))
            Text(stringResource(if (device.active) R.string.devices_active else R.string.devices_inactive))
            val seen = device.lastSeenAt
            Text(if (seen == null) stringResource(R.string.devices_never_seen)
                else stringResource(R.string.devices_last_seen, seen))
            if (onOpenPolicy != null) {
                OutlinedButton(onClick = { onOpenPolicy(device.id) }, enabled = device.active) {
                    Text(stringResource(R.string.devices_view_policy))
                }
            }
        }
    }
}
