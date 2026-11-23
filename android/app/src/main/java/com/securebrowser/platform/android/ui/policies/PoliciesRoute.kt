package com.securebrowser.platform.android.ui.policies

import androidx.activity.compose.BackHandler
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
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
    detailModel: PolicyDetailViewModel,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier = Modifier
) {
    var selectedId by remember { mutableStateOf<Long?>(null) }
    // Conserva la página mientras se consulta el detalle y límpiala al salir del catálogo.
    DisposableEffect(model) { onDispose { model.clearPage() } }
    val policyId = selectedId
    if (policyId != null) {
        val backToCatalog = { selectedId = null }
        BackHandler(onBack = backToCatalog)
        PolicyDetailRoute(detailModel, policyId, backToCatalog, onSignOut, onSessionExpired, modifier)
        return
    }

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

    PoliciesScreen(
        state = state,
        onOpenPolicy = { id -> selectedId = id },
        onPrevious = { scope.launch { model.previousPage() } },
        onNext = { scope.launch { model.nextPage() } },
        onRetry = { scope.launch { model.refresh() } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}

/** Consulta el detalle del ID elegido y vuelve a validar la sesión al regresar al primer plano. */
@Composable
private fun PolicyDetailRoute(
    model: PolicyDetailViewModel,
    id: Long,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier
) {
    val state by model.state.collectAsStateWithLifecycle()
    val lifecycle = LocalLifecycleOwner.current.lifecycle
    val scope = rememberCoroutineScope()
    val sessionExpired by rememberUpdatedState(onSessionExpired)

    DisposableEffect(model, id) { onDispose { model.clearDetail() } }
    LaunchedEffect(lifecycle, model, id) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { model.refresh(id) }
    }
    LaunchedEffect(state) {
        if (state == PolicyDetailState.SessionExpired) sessionExpired()
    }
    PolicyDetailScreen(
        state = state,
        onSave = { draft -> scope.launch { model.save(draft) } },
        onRetry = { scope.launch { model.refresh(id) } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}
