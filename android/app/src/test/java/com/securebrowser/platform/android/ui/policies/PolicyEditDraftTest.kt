package com.securebrowser.platform.android.ui.policies

import com.securebrowser.platform.android.core.network.createRetrofit
import com.securebrowser.platform.android.data.remote.FilterMode
import com.securebrowser.platform.android.data.remote.PolicyResponse
import com.securebrowser.platform.android.data.remote.PolicyUpdateRequest
import kotlinx.serialization.json.Json
import okio.Buffer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

/** Comprueba cómo pasan los cambios del formulario al cuerpo JSON del PATCH. */
class PolicyEditDraftTest {
    private val original = PolicyResponse(
        id = 1, companyId = 1, name = "Demo",
        urlFilteringEnabled = true, urlFilteringMode = FilterMode.DENYLIST,
        urlRules = listOf("blocked.example"),
        downloadControlEnabled = true, downloadControlMode = FilterMode.DENYLIST,
        downloadRules = listOf("bat", "exe"),
        createdAt = "2026-01-01T00:00:00Z", updatedAt = "2026-01-01T00:00:00Z",
    )
    private val draft = PolicyEditDraft.from(original)

    @Test
    fun restoringOriginalRulesProducesNoRequest() {
        val edited = draft.copy(downloadRulesText = "exe")
        val restored = edited.copy(downloadRulesText = " bat \n\nexe\n")

        assertNull(restored.toUpdateRequest(original))
    }

    @Test
    fun disablingDownloadsAndEditingDomainsSendsOnlyThoseFields() {
        val edited = draft.copy(downloadControlEnabled = false, urlRulesText = " as.com \n\nejemplo.com")

        assertPatchJson(edited, """{"downloadControlEnabled":false,"urlRules":["as.com","ejemplo.com"]}""")
    }

    @Test
    fun clearingDownloadRulesSendsAnEmptyArray() {
        assertPatchJson(draft.copy(downloadRulesText = ""), """{"downloadRules":[]}""")
    }

    /** Lee el cuerpo escrito por el mismo conversor que utiliza la aplicación. */
    private fun assertPatchJson(edited: PolicyEditDraft, expected: String) {
        val request = requireNotNull(edited.toUpdateRequest(original))
        val converter = createRetrofit("https://example.invalid/").requestBodyConverter<PolicyUpdateRequest>(
            PolicyUpdateRequest::class.java, emptyArray(), emptyArray()
        )
        val buffer = Buffer()
        requireNotNull(converter.convert(request)).writeTo(buffer)
        assertEquals(Json.parseToJsonElement(expected), Json.parseToJsonElement(buffer.readUtf8()))
    }
}
