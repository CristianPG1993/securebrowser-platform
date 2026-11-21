package com.securebrowser.platform.android

import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.securebrowser.platform.android.core.network.ApiResult
import kotlinx.coroutines.runBlocking
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class LoginBackendTest {
    @get:Rule val compose = createAndroidComposeRule<MainActivity>()

    /** Verifica que el rechazo real de Spring Boot llega hasta el mensaje visible del formulario. */
    @Test
    fun loginScreenShowsInvalidCredentials() {
        val activity = compose.activity
        compose.onNodeWithText(activity.getString(R.string.login_email)).performTextInput("android-test@example.invalid")
        compose.onNodeWithText(activity.getString(R.string.login_password)).performTextInput("test-only")
        compose.onNodeWithText(activity.getString(R.string.login_submit)).performClick()
        val rejection = activity.getString(R.string.login_invalid_credentials)
        compose.waitUntil(15_000) { compose.onAllNodes(androidx.compose.ui.test.hasText(rejection)).fetchSemanticsNodes().isNotEmpty() }
        compose.onNodeWithText(rejection).assertIsDisplayed()
    }

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
