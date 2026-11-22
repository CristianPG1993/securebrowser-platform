package com.securebrowser.platform.android.data.remote

import com.securebrowser.platform.android.core.network.createRetrofit
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import okhttp3.mockwebserver.MockResponse
import okhttp3.mockwebserver.MockWebServer
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test

/** Verifica que Retrofit solicita una página concreta y lee las instalaciones que contiene. */
class DeviceApiTest {
    @get:Rule val server = MockWebServer()

    @Test
    fun listDevicesSendsBearerAndReadsRequestedPage() = runBlocking {
        server.enqueue(MockResponse().setHeader("Content-Type", "application/json").setBody("""
            {
              "items": [{"id":11,"deviceIdentifier":"test-device","name":null,"active":true,"lastSeenAt":null}],
              "page":1, "size":10, "totalElements":11, "totalPages":2
            }
        """.trimIndent()))
        val api = createRetrofit(server.url("/api/v1/").toString()).create(DeviceApi::class.java)
        val response = api.listDevices("Bearer test-access", page = 1, size = 10)

        val request = requireNotNull(server.takeRequest(1, TimeUnit.SECONDS))
        assertEquals("GET", request.method)
        assertEquals("/api/v1/devices?page=1&size=10", request.path)
        assertEquals("Bearer test-access", request.getHeader("Authorization"))
        val page = requireNotNull(response.body())
        assertEquals(1, page.page)
        assertEquals(2, page.totalPages)
        val device = page.items.single()
        assertEquals(11L, device.id)
        assertNull(device.lastSeenAt)
    }
}
