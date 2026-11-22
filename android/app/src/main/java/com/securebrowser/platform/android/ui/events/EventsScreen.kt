package com.securebrowser.platform.android.ui.events

import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
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
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedCard
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
import com.securebrowser.platform.android.data.remote.SecurityEventType

/** Muestra la página del historial y comunica los cambios de filtro y página a la ruta. */
@Composable
internal fun EventsScreen(
    state: EventsState,
    onSelectType: (SecurityEventType?) -> Unit,
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
        item { Text(stringResource(R.string.events_title), style = MaterialTheme.typography.headlineMedium) }
        if (state.content != EventsContent.Forbidden && state.content != EventsContent.SessionExpired) {
            item { EventTypeFilter(state.type, onSelectType) }
        }
        when (val content = state.content) {
            EventsContent.Loading -> item { CircularProgressIndicator() }
            EventsContent.Error -> {
                item { Text(stringResource(R.string.events_load_error)) }
                item { Button(onClick = onRetry) { Text(stringResource(R.string.dashboard_retry)) } }
            }
            EventsContent.Forbidden -> item { Text(stringResource(R.string.events_forbidden)) }
            EventsContent.SessionExpired -> item { Text(stringResource(R.string.login_session_expired)) }
            is EventsContent.Ready -> {
                val page = content.page
                if (page.items.isEmpty()) item { Text(stringResource(R.string.events_empty)) }
                items(page.items, key = { it.id }) { event ->
                    OutlinedCard(modifier = Modifier.fillMaxWidth()) {
                        Column(Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            Text(stringResource(typeLabel(event.type)), style = MaterialTheme.typography.titleMedium)
                            Text(stringResource(R.string.events_device, event.deviceId))
                            Text(event.occurredAt)
                        }
                    }
                }
                val hasPrevious = page.page > 0
                val hasNext = page.page < page.totalPages - 1
                if (hasPrevious || hasNext) {
                    if (page.page < page.totalPages) item {
                        Text(stringResource(R.string.devices_page, page.page + 1, page.totalPages))
                    }
                    item {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            OutlinedButton(onClick = onPrevious, enabled = hasPrevious) {
                                Text(stringResource(R.string.devices_previous))
                            }
                            OutlinedButton(onClick = onNext, enabled = hasNext) {
                                Text(stringResource(R.string.devices_next))
                            }
                        }
                    }
                }
            }
        }
        if (state.content != EventsContent.SessionExpired) {
            item { OutlinedButton(onClick = onBack) { Text(stringResource(R.string.dashboard_return)) } }
            item { OutlinedButton(onClick = onSignOut) { Text(stringResource(R.string.logout_submit)) } }
        }
    }
}

/** La opción Todos envía null para consultar el historial sin filtro de tipo. */
@Composable
private fun EventTypeFilter(selected: SecurityEventType?, onSelect: (SecurityEventType?) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        OutlinedButton(onClick = { expanded = true }) {
            Text(stringResource(R.string.events_filter, stringResource(typeLabel(selected))))
        }
        DropdownMenu(expanded = expanded, onDismissRequest = { expanded = false }) {
            for (type in listOf(null) + SecurityEventType.entries) {
                DropdownMenuItem(
                    text = { Text(stringResource(typeLabel(type))) },
                    onClick = {
                        expanded = false
                        onSelect(type)
                    }
                )
            }
        }
    }
}

@StringRes
private fun typeLabel(type: SecurityEventType?): Int = when (type) {
    null -> R.string.events_all_types
    SecurityEventType.URL_BLOCKED -> R.string.events_url_blocked
    SecurityEventType.DOWNLOAD_BLOCKED -> R.string.events_download_blocked
    SecurityEventType.POLICY_UPDATED -> R.string.events_policy_updated
}
