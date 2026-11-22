package com.securebrowser.platform.android.ui.devices

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.remote.DevicePageResponse
import com.securebrowser.platform.android.data.remote.DeviceResponse
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.async
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

/** Verifica la navegación entre páginas, los reintentos y los estados visibles sin un backend real. */
class DevicesViewModelTest {
    @Test
    fun retriesFailedPageAndRespectsPageBounds() = runBlocking {
        val requests = mutableListOf<Int>()
        val device = DeviceResponse(3, "test-device", active = true)
        var offline = false
        val model = DevicesViewModel { page, size ->
            requests.add(page)
            assertEquals(20, size)
            if (offline) ApiResult.ConnectionError
            else ApiResult.Success(DevicePageResponse(List(if (page == 0) 20 else 1) { device }, page, size, 21, 2))
        }
        model.refresh()
        assertNull((model.state.value as DevicesState.Ready).page.items.first().lastSeenAt)
        model.previousPage()
        offline = true
        model.nextPage()
        assertEquals(DevicesState.Error, model.state.value)
        offline = false
        model.refresh()
        assertEquals(1, (model.state.value as DevicesState.Ready).page.page)
        model.nextPage()
        model.previousPage()
        assertEquals(listOf(0, 1, 1, 0), requests)
    }

    @Test
    fun emptyPageAndExpiredSessionHaveDifferentStates() = runBlocking {
        var result: ApiResult<DevicePageResponse> = ApiResult.Success(DevicePageResponse(emptyList(), 0, 20, 0, 0))
        val model = DevicesViewModel { _, _ -> result }
        model.refresh()
        val ready = model.state.value as DevicesState.Ready
        assertTrue(ready.isEmpty)
        assertFalse(ready.hasNext)
        assertFalse(ready.hasPrevious)
        result = ApiResult.HttpError(401, null)
        model.refresh()
        assertEquals(DevicesState.SessionExpired, model.state.value)
    }

    @Test
    fun repeatedRefreshWhileLoadingSendsOneRequest() = runBlocking {
        val response = CompletableDeferred<ApiResult<DevicePageResponse>>()
        var calls = 0
        val model = DevicesViewModel { _, _ -> calls++; response.await() }
        val first = async(start = CoroutineStart.UNDISPATCHED) { model.refresh() }
        model.refresh()
        assertEquals(DevicesState.Loading, model.state.value)
        assertEquals(1, calls)
        response.complete(ApiResult.ConnectionError)
        first.await()
        assertEquals(DevicesState.Error, model.state.value)
    }
}
