package com.securebrowser.platform.android.ui.navigation

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.DevicePageResponse
import com.securebrowser.platform.android.data.remote.DeviceResponse
import com.securebrowser.platform.android.data.remote.PolicyPageResponse
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import com.securebrowser.platform.android.data.remote.UserRole
import com.securebrowser.platform.android.ui.dashboard.DashboardState
import com.securebrowser.platform.android.ui.devices.DevicesRoute
import com.securebrowser.platform.android.ui.devices.DevicesViewModel
import com.securebrowser.platform.android.ui.policies.PoliciesRoute
import com.securebrowser.platform.android.ui.policies.PoliciesViewModel
import org.junit.Rule
import org.junit.Test

/** Comprueba los accesos visibles y el regreso al panel con cada rol. */
class DashboardNavigationTest {
    @get:Rule val compose = createComposeRule()

    private fun openDashboard(role: UserRole) {
        val profile = UserProfileResponse("Ana", "Díaz", "ana@example.invalid", role)
        val devices = DevicesViewModel { page, size ->
            val device = DeviceResponse(7, "test-installation", active = true)
            ApiResult.Success(DevicePageResponse(listOf(device), page, size, 1, 1))
        }
        val policies = PoliciesViewModel { page, size ->
            ApiResult.Success(PolicyPageResponse(emptyList(), page, size, 0, 0))
        }
        compose.setContent {
            DashboardNavigation(
                state = DashboardState.Ready(profile),
                onRefreshProfile = {},
                onSignOut = {},
                devicesScreen = { onBack -> DevicesRoute(devices, onBack, {}, {}) },
                policiesScreen = { onBack -> PoliciesRoute(policies, null, onBack, {}, {}) }
            )
        }
    }

    @Test
    fun userCanOpenDevicesAndEvents() {
        openDashboard(UserRole.USER)
        compose.onNodeWithText("Catálogo de políticas").assertDoesNotExist()
        for (title in listOf("Dispositivos", "Eventos de seguridad")) {
            compose.onNodeWithText(title).performClick()
            compose.onNodeWithText(title).assertIsDisplayed()
            if (title == "Dispositivos") {
                compose.onNodeWithText("ID 7").assertIsDisplayed()
                compose.onNodeWithText("Sin comunicación registrada").assertIsDisplayed()
            }
            compose.onNodeWithText("Volver al panel").performClick()
            compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
        }
    }

    @Test
    fun adminCanOpenPolicyCatalog() {
        openDashboard(UserRole.ADMIN)
        compose.onNodeWithText("Catálogo de políticas").performClick()
        compose.onNodeWithText("No hay políticas en esta página.").assertIsDisplayed()
        compose.onNodeWithText("Volver al panel").assertIsDisplayed().performClick()
        compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
    }
}
