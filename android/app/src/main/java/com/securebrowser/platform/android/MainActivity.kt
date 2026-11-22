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
import com.securebrowser.platform.android.data.remote.UserRole
import com.securebrowser.platform.android.ui.dashboard.DashboardState
import com.securebrowser.platform.android.ui.dashboard.DashboardViewModel
import com.securebrowser.platform.android.ui.devices.DevicesRoute
import com.securebrowser.platform.android.ui.devices.DevicesViewModel
import com.securebrowser.platform.android.ui.login.LoginScreen
import com.securebrowser.platform.android.ui.login.LoginStatus
import com.securebrowser.platform.android.ui.login.LoginViewModel
import com.securebrowser.platform.android.ui.navigation.DashboardNavigation
import com.securebrowser.platform.android.ui.policies.PoliciesRoute
import com.securebrowser.platform.android.ui.policies.PoliciesViewModel
import com.securebrowser.platform.android.ui.policies.PolicyDetailViewModel
import com.securebrowser.platform.android.ui.theme.SecureBrowserAndroidTheme

/** Conecta el formulario de acceso con el repositorio de la aplicación. */
class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        val repositories = application as? RepositoryProvider
        setContent {
            SecureBrowserAndroidTheme {
                Scaffold(modifier = Modifier.fillMaxSize()) { insets ->
                    if (repositories == null) {
                        Text(stringResource(R.string.login_unconfigured), Modifier.padding(insets))
                    } else {
                        val repository = repositories.authRepository
                        val model = viewModel {
                            LoginViewModel(repository::login, repository::logout, repository.hasSession)
                        }
                        val status by model.status.collectAsStateWithLifecycle()
                        if (status == LoginStatus.Authenticated) {
                            val dashboard = viewModel { DashboardViewModel(repository::loadProfile) }
                            val state by dashboard.state.collectAsStateWithLifecycle()
                            val signOut = {
                                dashboard.clearProfile()
                                model.signOut()
                            }
                            val sessionExpired = {
                                model.onSessionExpired()
                                dashboard.clearProfile()
                            }
                            DashboardNavigation(
                                state = state,
                                onRefreshProfile = {
                                    dashboard.refresh()
                                    if (dashboard.state.value == DashboardState.SessionExpired) {
                                        sessionExpired()
                                    }
                                },
                                onSignOut = signOut,
                                modifier = Modifier.padding(insets),
                                devicesScreen = { onBack ->
                                    val devices = viewModel { DevicesViewModel(repositories.deviceRepository::loadPage) }
                                    val assignedPolicy = if ((state as? DashboardState.Ready)?.profile?.role == UserRole.USER) {
                                        viewModel(key = "deviceAssignedPolicy") {
                                            PolicyDetailViewModel(repositories.deviceRepository::loadAssignedPolicy)
                                        }
                                    } else null
                                    DevicesRoute(devices, assignedPolicy, onBack, signOut, sessionExpired, Modifier.padding(insets))
                                },
                                policiesScreen = { onBack ->
                                    val policies = viewModel { PoliciesViewModel(repositories.policyRepository::loadPage) }
                                    val detail = viewModel { PolicyDetailViewModel(repositories.policyRepository::loadPolicy) }
                                    PoliciesRoute(
                                        model = policies,
                                        detailModel = detail,
                                        onBack = onBack,
                                        onSignOut = signOut,
                                        onSessionExpired = sessionExpired,
                                        modifier = Modifier.padding(insets)
                                    )
                                }
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
