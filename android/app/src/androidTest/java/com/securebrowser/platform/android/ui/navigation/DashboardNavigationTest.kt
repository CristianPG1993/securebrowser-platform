package com.securebrowser.platform.android.ui.navigation

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import com.securebrowser.platform.android.data.remote.UserRole
import com.securebrowser.platform.android.ui.dashboard.DashboardState
import org.junit.Rule
import org.junit.Test

/** Comprueba los accesos visibles y el regreso al panel con cada rol. */
class DashboardNavigationTest {
    @get:Rule val compose = createComposeRule()

    private fun openDashboard(role: UserRole) {
        val profile = UserProfileResponse("Ana", "Díaz", "ana@example.invalid", role)
        compose.setContent { DashboardNavigation(DashboardState.Ready(profile), {}, {}) }
    }

    @Test
    fun userCanOpenDevicesAndEvents() {
        openDashboard(UserRole.USER)
        compose.onNodeWithText("Catálogo de políticas").assertDoesNotExist()
        for (title in listOf("Dispositivos", "Eventos de seguridad")) {
            compose.onNodeWithText(title).performClick()
            compose.onNodeWithText(title).assertIsDisplayed()
            compose.onNodeWithText("Volver al panel").performClick()
            compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
        }
    }

    @Test
    fun adminCanOpenPolicyCatalog() {
        openDashboard(UserRole.ADMIN)
        compose.onNodeWithText("Catálogo de políticas").performClick()
        compose.onNodeWithText("Volver al panel").assertIsDisplayed().performClick()
        compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
    }
}
