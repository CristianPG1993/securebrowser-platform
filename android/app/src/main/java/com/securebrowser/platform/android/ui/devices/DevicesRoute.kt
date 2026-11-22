package com.securebrowser.platform.android.ui.devices

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch

/** Conecta las acciones de Compose con el listado de la sesión autenticada. */
@Composable
internal fun DevicesRoute(
    model: DevicesViewModel,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state by model.state.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val lifecycle = LocalLifecycleOwner.current.lifecycle

    suspend fun request(action: suspend () -> Unit) {
        action()
        if (model.state.value == DevicesState.SessionExpired) onSessionExpired()
    }

    DisposableEffect(model) { onDispose { model.clearPage() } }
    LaunchedEffect(lifecycle, model) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { request(model::refresh) }
    }
    DevicesScreen(
        state = state,
        onPrevious = { scope.launch { request(model::previousPage) } },
        onNext = { scope.launch { request(model::nextPage) } },
        onRetry = { scope.launch { request(model::refresh) } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}
