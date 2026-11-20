package com.securebrowser.platform.android

import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.core.network.executeApiCall
import com.securebrowser.platform.android.data.remote.LoginRequest
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/** Comprueba desde el emulador el login contra el backend local publicado por Docker Compose. */
@RunWith(AndroidJUnit4::class)
class LoginBackendTest {
    /** El rechazo de credenciales ficticias confirma que la petición llega al endpoint de autenticación. */
    @Test
    fun loginWithUnknownUserReturnsInvalidCredentials() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val app = context.applicationContext as DebugApplication
        val credentials = LoginRequest("android-test@example.invalid", "test-only")

        val result = runBlocking {
            executeApiCall { app.authApi.login(credentials) }
        }
        assertEquals(ApiResult.HttpError(401, "INVALID_CREDENTIALS"), result)
    }
}
