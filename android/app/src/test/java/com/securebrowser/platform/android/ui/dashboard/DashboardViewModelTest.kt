package com.securebrowser.platform.android.ui.dashboard

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import com.securebrowser.platform.android.data.remote.UserRole
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/** Comprueba los estados que MainActivity entrega al Dashboard tras consultar el perfil. */
class DashboardViewModelTest {
    @Test
    fun retryDisplaysProfileForEitherRoleAndSignOutClearsIt() = runBlocking {
        for (role in UserRole.entries) {
            val profile = UserProfileResponse("Ana", "Díaz", "ana@example.invalid", role)
            var result: ApiResult<UserProfileResponse> = ApiResult.ConnectionError
            val model = DashboardViewModel { result }
            assertEquals(DashboardState.Loading, model.state.value)
            model.refresh()
            assertEquals(DashboardState.Error, model.state.value)
            result = ApiResult.Success(profile)
            model.refresh()
            assertSame(profile, (model.state.value as DashboardState.Ready).profile)
            model.clearProfile()
            assertEquals(DashboardState.Loading, model.state.value)
        }
    }

    @Test
    fun unauthorizedResponseRequestsReturnToLogin() = runBlocking {
        val model = DashboardViewModel { ApiResult.HttpError(401, null) }
        model.refresh()
        assertEquals(DashboardState.SessionExpired, model.state.value)
    }
}
