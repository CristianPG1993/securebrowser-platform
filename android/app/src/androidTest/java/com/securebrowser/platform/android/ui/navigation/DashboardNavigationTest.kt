package com.securebrowser.platform.android.ui.navigation

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.hasScrollAction
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollToNode
import androidx.test.espresso.Espresso.pressBack
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.DevicePageResponse
import com.securebrowser.platform.android.data.remote.DeviceResponse
import com.securebrowser.platform.android.data.remote.FilterMode
import com.securebrowser.platform.android.data.remote.PolicyPageResponse
import com.securebrowser.platform.android.data.remote.PolicyResponse
import com.securebrowser.platform.android.data.remote.SecurityEventPageResponse
import com.securebrowser.platform.android.data.remote.SecurityEventResponse
import com.securebrowser.platform.android.data.remote.SecurityEventType
import com.securebrowser.platform.android.data.remote.UserProfileResponse
import com.securebrowser.platform.android.data.remote.UserRole
import com.securebrowser.platform.android.ui.dashboard.DashboardState
import com.securebrowser.platform.android.ui.devices.DevicesRoute
import com.securebrowser.platform.android.ui.devices.DevicesViewModel
import com.securebrowser.platform.android.ui.events.EventsRoute
import com.securebrowser.platform.android.ui.events.EventsViewModel
import com.securebrowser.platform.android.ui.policies.PoliciesRoute
import com.securebrowser.platform.android.ui.policies.PoliciesViewModel
import com.securebrowser.platform.android.ui.policies.PolicyDetailViewModel
import org.junit.Rule
import org.junit.Test

/** Comprueba los accesos visibles y el regreso al panel con cada rol. */
class DashboardNavigationTest {
    @get:Rule val compose = createComposeRule()

    private fun openDashboard(role: UserRole, catalog: List<PolicyResponse> = emptyList()) {
        val profile = UserProfileResponse("Ana", "Díaz", "ana@example.invalid", role)
        val devices = DevicesViewModel { page, size ->
            val device = DeviceResponse(7, "test-installation", active = true)
            ApiResult.Success(DevicePageResponse(listOf(device), page, size, 1, 1))
        }
        val policies = PoliciesViewModel { page, size ->
            val items = catalog.drop(page * size).take(size)
            val totalPages = (catalog.size + size - 1) / size
            ApiResult.Success(PolicyPageResponse(items, page, size, catalog.size.toLong(), totalPages))
        }
        val detail = PolicyDetailViewModel { id -> ApiResult.Success(catalog.single { it.id == id }) }
        val events = EventsViewModel { page, size, type ->
            val event = SecurityEventResponse(1, 7, SecurityEventType.URL_BLOCKED, "2026-11-22T10:00:00Z")
            val items = listOf(event).filter { type == null || it.type == type }
            ApiResult.Success(SecurityEventPageResponse(items, page, size, items.size.toLong(), items.size))
        }
        val assignedPolicy = if (role == UserRole.USER) PolicyDetailViewModel { deviceId ->
            check(deviceId == 7L)
            ApiResult.Success(catalog.single())
        } else null
        compose.setContent {
            DashboardNavigation(
                state = DashboardState.Ready(profile),
                onRefreshProfile = {},
                onSignOut = {},
                devicesScreen = { onBack -> DevicesRoute(devices, assignedPolicy, onBack, {}, {}) },
                policiesScreen = { onBack -> PoliciesRoute(policies, detail, onBack, {}, {}) },
                eventsScreen = { onBack -> EventsRoute(events, onBack, {}, {}) }
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
            } else {
                compose.onNodeWithText("Dispositivo 7").assertIsDisplayed()
                compose.onNodeWithText("Tipo: Todos").performClick()
                compose.onNodeWithText("Descarga bloqueada").performClick()
                compose.onNodeWithText("No hay eventos en esta página.").assertIsDisplayed()
            }
            compose.onNodeWithText("Volver al panel").performClick()
            compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
        }
    }

    @Test
    fun userCanOpenAssignedPolicyFromDeviceAndReturn() {
        val policy = PolicyResponse(23, 1, "Política asignada", true, FilterMode.DENYLIST, listOf("device.example"),
            false, FilterMode.DENYLIST, emptyList(), "", "")
        openDashboard(UserRole.USER, listOf(policy))
        compose.onNodeWithText("Dispositivos").performClick()
        compose.onNodeWithText("Ver política").performClick()
        compose.onNodeWithText("Política asignada").assertIsDisplayed()
        compose.onNode(hasScrollAction()).performScrollToNode(hasText("Volver a dispositivos"))
        compose.onNodeWithText("Volver a dispositivos").performClick()
        compose.onNodeWithText("ID 7").assertIsDisplayed()
    }

    @Test
    fun adminCanOpenPolicyCatalog() {
        openDashboard(UserRole.ADMIN)
        compose.onNodeWithText("Catálogo de políticas").performClick()
        compose.onNodeWithText("No hay políticas en esta página.").assertIsDisplayed()
        compose.onNodeWithText("Volver al panel").assertIsDisplayed().performClick()
        compose.onNodeWithText("Panel de usuario").assertIsDisplayed()
    }

    @Test
    fun selectedPolicyOpensAndBothBackActionsRestoreItsCatalogPage() {
        val catalog = (1L..21L).map { id ->
            PolicyResponse(id, 1, "Política $id", true, FilterMode.DENYLIST, listOf("rule-$id.example"),
                false, FilterMode.DENYLIST, emptyList(), "", "")
        }
        openDashboard(UserRole.ADMIN, catalog)
        compose.onNodeWithText("Catálogo de políticas").performClick()
        compose.onNode(hasScrollAction()).performScrollToNode(hasText("Siguiente"))
        compose.onNodeWithText("Siguiente").performClick()
        compose.onNodeWithText("Política 21").performClick()
        compose.onNodeWithText("rule-21.example").assertIsDisplayed()
        compose.onNode(hasScrollAction()).performScrollToNode(hasText("Volver al catálogo"))
        compose.onNodeWithText("Volver al catálogo").performClick()
        compose.onNodeWithText("Política 21").performClick()
        compose.onNodeWithText("rule-21.example").assertIsDisplayed()
        pressBack()
        compose.onNodeWithText("Página 2 de 2").assertIsDisplayed()
    }
}
