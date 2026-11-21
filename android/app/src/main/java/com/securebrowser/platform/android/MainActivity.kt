package com.securebrowser.platform.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.securebrowser.platform.android.ui.dashboard.DashboardScreen
import com.securebrowser.platform.android.ui.dashboard.DashboardState
import com.securebrowser.platform.android.ui.dashboard.DashboardViewModel
import com.securebrowser.platform.android.ui.login.LoginScreen
import com.securebrowser.platform.android.ui.login.LoginStatus
import com.securebrowser.platform.android.ui.login.LoginViewModel
import com.securebrowser.platform.android.ui.theme.SecureBrowserAndroidTheme

/** Conecta el formulario de acceso con el repositorio de la aplicación. */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val repository = (application as? AuthRepositoryProvider)?.authRepository
        setContent {
            SecureBrowserAndroidTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { insets ->
                    if (repository == null) {
                        Text(stringResource(R.string.login_unconfigured), Modifier.padding(insets))
                    } else {
                        val model = viewModel {
                            LoginViewModel(repository::login, repository::logout, repository.hasSession)
                        }
                        val status by model.status.collectAsStateWithLifecycle()
                        if (status == LoginStatus.Authenticated) {
                            val dashboard = viewModel { DashboardViewModel(repository::loadProfile) }
                            val state by dashboard.state.collectAsStateWithLifecycle()
                            val lifecycle = LocalLifecycleOwner.current.lifecycle
                            var retry by remember { mutableIntStateOf(0) }
                            // Una consulta carga el perfil y comprueba la sesión al entrar, volver o reintentar.
                            LaunchedEffect(lifecycle, dashboard, retry) {
                                lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) {
                                    dashboard.refresh()
                                    if (dashboard.state.value == DashboardState.SessionExpired) {
                                        model.onSessionExpired()
                                        dashboard.clearProfile()
                                    }
                                }
                            }
                            DashboardScreen(
                                state = state,
                                onRetry = { retry++ },
                                onSignOut = {
                                    dashboard.clearProfile()
                                    model.signOut()
                                },
                                modifier = Modifier.padding(insets)
                            )
                        } else {
                            LoginScreen(status, model::submit, model::clearError, Modifier.padding(insets))
                        }
                    }
                }
            }
        }
    }
}
