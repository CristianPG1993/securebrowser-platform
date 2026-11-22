package com.securebrowser.platform.android.ui.events

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

/** Carga el historial en primer plano y conecta las acciones de la pantalla con el ViewModel. */
@Composable
internal fun EventsRoute(
    model: EventsViewModel,
    onBack: () -> Unit,
    onSignOut: () -> Unit,
    onSessionExpired: () -> Unit,
    modifier: Modifier = Modifier
) {
    val state by model.state.collectAsStateWithLifecycle()
    val scope = rememberCoroutineScope()
    val lifecycle = LocalLifecycleOwner.current.lifecycle

    DisposableEffect(model) { onDispose { model.clearPage() } }
    LaunchedEffect(lifecycle, model) {
        lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { model.refresh() }
    }
    LaunchedEffect(state.content) {
        if (state.content == EventsContent.SessionExpired) onSessionExpired()
    }
    EventsScreen(
        state = state,
        onSelectType = { type -> scope.launch { model.selectType(type) } },
        onPrevious = { scope.launch { model.previousPage() } },
        onNext = { scope.launch { model.nextPage() } },
        onRetry = { scope.launch { model.refresh() } },
        onBack = onBack,
        onSignOut = onSignOut,
        modifier = modifier
    )
}
