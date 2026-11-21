package com.securebrowser.platform.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.securebrowser.platform.android.core.network.ApiResult
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class LoginBackendTest {
    @Test
    fun loginWithUnknownUserReturnsInvalidCredentials() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val app = context.applicationContext as DebugApplication
        val result = runBlocking {
            app.authRepository.login("android-test@example.invalid", "test-only")
        }
        assertEquals(ApiResult.HttpError(401, "INVALID_CREDENTIALS"), result)
    }
}
