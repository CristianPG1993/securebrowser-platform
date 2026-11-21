package com.securebrowser.platform.android.ui.dashboard

import androidx.lifecycle.ViewModel
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow

/** Solo el estado Ready contiene datos del usuario. */
internal sealed interface DashboardState {
    data object Loading : DashboardState
    class Ready(val profile: UserProfileResponse) : DashboardState
    data object Error : DashboardState
    data object SessionExpired : DashboardState
}

/** Convierte la respuesta del repositorio en el estado que observa la pantalla. */
internal class DashboardViewModel(
    private val loadProfile: suspend () -> ApiResult<UserProfileResponse>?
) : ViewModel() {
    private val current = MutableStateFlow<DashboardState>(DashboardState.Loading)
    val state = current.asStateFlow()

    /** Deja el Dashboard preparado para la siguiente sesión al salir o caducar el acceso. */
    fun clearProfile() {
        current.value = DashboardState.Loading
    }

    suspend fun refresh() {
        current.value = DashboardState.Loading
        // El repositorio descarta una respuesta que pertenece a una sesión anterior.
        val result = loadProfile() ?: return
        current.value = when (result) {
            is ApiResult.Success -> DashboardState.Ready(result.value)
            is ApiResult.HttpError -> if (result.status == 401) DashboardState.SessionExpired else DashboardState.Error
            ApiResult.ConnectionError, ApiResult.InvalidResponse -> DashboardState.Error
        }
    }
}
