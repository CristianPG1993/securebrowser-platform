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
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.repeatOnLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
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
                            LoginViewModel(repository::login, repository::logout, repository::expireIfUnauthorized, repository.hasSession)
                        }
                        val status by model.status.collectAsStateWithLifecycle()
                        if (status == LoginStatus.Authenticated) {
                            val lifecycle = LocalLifecycleOwner.current.lifecycle
                            // Comprueba la sesión al entrar y cada vez que la actividad vuelve al primer plano.
                            LaunchedEffect(lifecycle, model) {
                                lifecycle.repeatOnLifecycle(Lifecycle.State.RESUMED) { model.checkSession() }
                            }
                        }
                        LoginScreen(status, model::submit, model::clearError, model::signOut, Modifier.padding(insets))
                    }
                }
            }
        }
    }
}
