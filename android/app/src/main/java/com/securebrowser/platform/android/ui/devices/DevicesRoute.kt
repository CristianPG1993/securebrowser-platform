package com.securebrowser.platform.android.ui.devices

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.ui.policies.PolicyDetailScreen
import com.securebrowser.platform.android.ui.policies.PolicyDetailState
import com.securebrowser.platform.android.ui.policies.PolicyDetailViewModel
import kotlinx.coroutines.launch

/** Conecta las acciones de Compose con el listado de la sesión autenticada. */
@Composable
internal fun DevicesRoute(
    model: DevicesViewModel,
    assignedPolicy: PolicyDetailViewModel?,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state by model.state.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    var selectedDeviceId by remember { mutableStateOf<Long?>(null) }

    suspend fun request(action: suspend () -> Unit) {
        action()
        if (model.state.value == DevicesState.SessionExpired) onSessionExpired()
    }

    DisposableEffect(model) { onDispose { model.clearPage() } }
    LaunchedEffect(lifecycle, model) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { request(model::refresh) }
    }
    val deviceId = selectedDeviceId
    if (deviceId != null && assignedPolicy != null) {
        val detailState by assignedPolicy.state.collectAsStateWithLifecycle()
        // Consultar la política registra lastSeenAt. Volver al primer plano solo refresca el listado.
        LaunchedEffect(deviceId, assignedPolicy) { assignedPolicy.refresh(deviceId) }
        DisposableEffect(assignedPolicy) { onDispose { assignedPolicy.clearDetail() } }
        LaunchedEffect(detailState) {
            if (detailState == PolicyDetailState.SessionExpired) onSessionExpired()
        }
        val backToDevices: () -> Unit = {
            selectedDeviceId = null
            scope.launch { request(model::refresh) }
        }
        BackHandler(onBack = backToDevices)
        PolicyDetailScreen(
            state = detailState,
            onRetry = { scope.launch { assignedPolicy.refresh(deviceId) } },
            onBack = backToDevices,
            onSignOut = onSignOut,
            modifier = modifier,
            backLabel = R.string.policy_back_to_devices
        )
        return
    }
    DevicesScreen(
        state = state,
        onOpenPolicy = if (assignedPolicy != null) { id -> selectedDeviceId = id } else null,
        onPrevious = { scope.launch { request(model::previousPage) } },
        onNext = { scope.launch { request(model::nextPage) } },
        onRetry = { scope.launch { request(model::refresh) } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}
