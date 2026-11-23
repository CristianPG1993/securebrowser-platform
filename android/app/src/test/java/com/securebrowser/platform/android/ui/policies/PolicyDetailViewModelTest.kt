package com.securebrowser.platform.android.ui.policies

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.FilterMode
import com.securebrowser.platform.android.data.remote.PolicyResponse
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Assert.assertSame
import org.junit.Test

/** Comprueba el guardado y la confirmación del detalle con respuestas controladas. */
class PolicyDetailViewModelTest {
    @Test
    fun retryAfterFailedConfirmationReadsPolicyWithoutRepeatingPatch() = runBlocking {
        val original = policy(listOf("blocked.example"))
        val newRules = listOf("blocked.example", "as.com")
        val confirmed = policy(newRules)
        val calls = mutableListOf<String>()
        var readResult: ApiResult<PolicyResponse> = ApiResult.Success(original)
        val model = PolicyDetailViewModel(
            loadPolicy = { id ->
                calls.add("GET $id")
                readResult
            },
            updatePolicy = { id, changes ->
                calls.add("PATCH $id")
                assertEquals(newRules, changes.urlRules)
                ApiResult.Success(policy(newRules))
            }
        )

        model.refresh(original.id)
        val draft = PolicyEditDraft.from(original).copy(urlRulesText = "blocked.example\nas.com")

        // El PATCH se acepta, pero la primera lectura de confirmación falla.
        readResult = ApiResult.ConnectionError
        model.save(draft)
        assertEquals(PolicySaveStatus.ConfirmationFailed, (model.state.value as PolicyDetailState.Ready).saveStatus)

        readResult = ApiResult.Success(confirmed)
        model.refresh(original.id)

        val ready = model.state.value as PolicyDetailState.Ready
        assertEquals(listOf("GET 7", "PATCH 7", "GET 7", "GET 7"), calls)
        assertEquals(PolicySaveStatus.Confirmed, ready.saveStatus)
        assertSame(confirmed, ready.policy)
    }

    private fun policy(urlRules: List<String>) = PolicyResponse(
        id = 7, companyId = 1, name = "Demo",
        urlFilteringEnabled = true, urlFilteringMode = FilterMode.DENYLIST, urlRules = urlRules,
        downloadControlEnabled = true, downloadControlMode = FilterMode.DENYLIST, downloadRules = listOf("exe"),
        createdAt = "2026-01-01T00:00:00Z", updatedAt = "2026-01-01T00:00:00Z",
    )
}
