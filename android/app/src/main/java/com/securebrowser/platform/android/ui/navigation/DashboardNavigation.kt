package com.securebrowser.platform.android.ui.navigation

import androidx.activity.compose.BackHandler
import androidx.annotation.StringRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.repeatOnLifecycle
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
    onRefreshProfile: suspend () -> Unit,
    onSignOut: () -> Unit,
    modifier: Modifier = Modifier,
    devicesScreen: @Composable (onBack: () -> Unit) -> Unit,
    policiesScreen: @Composable (onBack: () -> Unit) -> Unit,
    eventsScreen: @Composable (onBack: () -> Unit) -> Unit
) {
    var selected by remember { mutableStateOf<DashboardDestination?>(null) }
    val role = (state as? DashboardState.Ready)?.profile?.role
    val destination = selected?.takeIf { role != null && it.availableTo(role) }
    BackHandler(enabled = destination != null) { selected = null }
    if (destination != null) {
        val onBack = { selected = null }
        when (destination) {
            DashboardDestination.Devices -> devicesScreen(onBack)
            DashboardDestination.Policies -> policiesScreen(onBack)
            DashboardDestination.Events -> eventsScreen(onBack)
        }
        return
    }
    // Los listados validan la sesión con su propia consulta al entrar en primer plano.
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val refreshProfile by rememberUpdatedState(onRefreshProfile)
    var retry by remember { mutableIntStateOf(0) }
    LaunchedEffect(lifecycle, retry) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { refreshProfile() }
    }
    DashboardScreen(state, { retry++ }, onSignOut, onOpen = { selected = it }, modifier = modifier)
}
