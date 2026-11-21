package com.securebrowser.platform.android.ui.login

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.LocalSaveableStateRegistry
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import com.securebrowser.platform.android.R

/** Formulario de acceso. Sus campos viven en memoria mientras están en pantalla. */
@Composable
internal fun LoginScreen(
    status: LoginStatus,
    onSubmit: (String, String) -> Unit,
    onEdit: () -> Unit,
    modifier: Modifier = Modifier
) {
    Box(modifier.fillMaxSize().imePadding(), contentAlignment = Alignment.Center) {
        if (status == LoginStatus.SigningOut) {
            Text(stringResource(R.string.logout_pending))
            return@Box
        }
        var email by remember { mutableStateOf("") }
        var password by remember { mutableStateOf("") }
        val focus = LocalFocusManager.current
        val loading = status == LoginStatus.Loading
        val submit = {
            focus.clearFocus()
            onSubmit(email, password)
        }
        Column(
            Modifier.widthIn(max = 440.dp).fillMaxWidth().verticalScroll(rememberScrollState()).padding(24.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(stringResource(R.string.login_title), style = MaterialTheme.typography.headlineMedium)
            when (status) {
                LoginStatus.SignedOut -> Text(stringResource(R.string.logout_confirmed))
                LoginStatus.SignedOutLocally -> Text(stringResource(R.string.logout_local_only))
                else -> Unit
            }
            // Los campos de texto y su estado interno quedan fuera del estado restaurable de Android.
            CompositionLocalProvider(LocalSaveableStateRegistry provides null) {
                OutlinedTextField(
                    value = email, onValueChange = { email = it; onEdit() },
                    modifier = Modifier.fillMaxWidth(), enabled = !loading, singleLine = true,
                    label = { Text(stringResource(R.string.login_email)) },
                    isError = status == LoginStatus.InvalidEmail,
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email, imeAction = ImeAction.Next)
                )
                OutlinedTextField(
                    value = password, onValueChange = { password = it; onEdit() },
                    modifier = Modifier.fillMaxWidth(), enabled = !loading, singleLine = true,
                    label = { Text(stringResource(R.string.login_password)) },
                    isError = status == LoginStatus.EmptyPassword,
                    visualTransformation = PasswordVisualTransformation(),
                    keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Password, imeAction = ImeAction.Done),
                    keyboardActions = KeyboardActions(onDone = { if (!loading) submit() })
                )
            }
            status.error?.let { message ->
                Text(stringResource(message), Modifier.semantics { liveRegion = LiveRegionMode.Polite },
                    color = MaterialTheme.colorScheme.error)
            }
            Button(onClick = submit, enabled = !loading, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(if (loading) R.string.login_loading else R.string.login_submit))
            }
        }
    }
}
