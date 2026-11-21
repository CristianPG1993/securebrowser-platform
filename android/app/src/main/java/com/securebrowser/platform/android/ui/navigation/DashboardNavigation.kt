package com.securebrowser.platform.android.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.data.remote.UserRole
import com.securebrowser.platform.android.ui.dashboard.DashboardScreen
import com.securebrowser.platform.android.ui.dashboard.DashboardState

/** El catálogo de políticas requiere ADMIN tanto para abrirlo como para mostrar su acceso. */
internal enum class DashboardDestination(@StringRes val title: Int) {
    Devices(R.string.devices_title),
    Policies(R.string.policies_title),
    Events(R.string.events_title);

    fun availableTo(role: UserRole) = this != Policies || role == UserRole.ADMIN
}

/** El destino vive en la composición de la sesión autenticada y se descarta al salir. */
@Composable
internal fun DashboardNavigation(
    state: DashboardState,
    onRetry: () -> Unit,
    onSignOut: () -> Unit,
    modifier: Modifier = Modifier
) {
    var selected by remember { mutableStateOf<DashboardDestination?>(null) }
    val role = (state as? DashboardState.Ready)?.profile?.role
    val destination = selected?.takeIf { role != null && it.availableTo(role) }
    BackHandler(enabled = destination != null) { selected = null }
    if (destination == null) {
        DashboardScreen(state, onRetry, onSignOut, onOpen = { selected = it }, modifier = modifier)
    } else {
        Column(modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
            Text(stringResource(destination.title), style = MaterialTheme.typography.headlineMedium)
            OutlinedButton(onClick = { selected = null }) { Text(stringResource(R.string.dashboard_return)) }
            OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) }
        }
    }
}
