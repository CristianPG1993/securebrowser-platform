package com.securebrowser.platform.android.ui.dashboard

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import com.securebrowser.platform.android.ui.navigation.DashboardDestination

/** Muestra el perfil de la sesión y delega el reintento y el cierre en quien presenta la pantalla. */
@Composable
internal fun DashboardScreen(
    state: DashboardState,
    onRetry: () -> Unit,
    onSignOut: () -> Unit,
    onOpen: (DashboardDestination) -> Unit,
    modifier: Modifier = Modifier
) {
    Column(
        modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(24.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp)
    ) {
        Text(stringResource(R.string.dashboard_title), style = MaterialTheme.typography.headlineMedium)
        when (state) {
            DashboardState.Loading -> CircularProgressIndicator()
            is DashboardState.Ready -> {
                val profile = state.profile
                Text(stringResource(R.string.dashboard_full_name, profile.name, profile.lastName),
                    style = MaterialTheme.typography.titleLarge)
                Text(profile.email)
                Text(stringResource(R.string.dashboard_role, profile.role.name))
                for (destination in DashboardDestination.entries.filter { it.availableTo(profile.role) }) {
                    Button(onClick = { onOpen(destination) }) { Text(stringResource(destination.title)) }
                }
            }
            DashboardState.Error -> {
                Text(stringResource(R.string.dashboard_profile_error))
                Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) }
            }
            DashboardState.SessionExpired -> {
                Text(stringResource(R.string.login_session_expired))
                return@Column
            }
        }
        OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) }
    }
}
