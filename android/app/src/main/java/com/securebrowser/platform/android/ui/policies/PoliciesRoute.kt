package com.securebrowser.platform.android.ui.policies

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import kotlinx.coroutines.launch

/** Conecta el estado del catálogo y las acciones de la pantalla con el ViewModel. */
@Composable
internal fun PoliciesRoute(
    model: PoliciesViewModel,
    onOpenPolicy: ((Long) -> Unit)?,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state by model.state.collectAsStateWithLifecycle()
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val scope = rememberCoroutineScope()
    val sessionExpired by rememberUpdatedState(onSessionExpired)

    // Al abrir el catálogo o volver al primer plano, consulta la página actual.
    LaunchedEffect(lifecycle, model) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { model.refresh() }
    }
    LaunchedEffect(state) {
        if (state == PoliciesState.SessionExpired) sessionExpired()
    }
    DisposableEffect(model) { onDispose { model.clearPage() } }

    PoliciesScreen(
        state = state,
        onOpenPolicy = onOpenPolicy,
        onPrevious = { scope.launch { model.previousPage() } },
        onNext = { scope.launch { model.nextPage() } },
        onRetry = { scope.launch { model.refresh() } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}
