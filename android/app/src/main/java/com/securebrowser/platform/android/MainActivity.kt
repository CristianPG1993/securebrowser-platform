package com.securebrowser.platform.android

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.securebrowser.platform.android.ui.login.LoginScreen
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
                        val model = viewModel { LoginViewModel(repository::login, repository.hasSession) }
                        val status by model.status.collectAsStateWithLifecycle()
                        LoginScreen(status, model::submit, model::clearError, Modifier.padding(insets))
                    }
                }
            }
        }
    }
}
