package com.securebrowser.platform.android.ui.login

import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.repository.LogoutResult
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test

/** Comprueba los estados del formulario con una respuesta de login controlada. */
@OptIn(ExperimentalCoroutinesApi::class)
class LoginViewModelTest {
    private val dispatcher = StandardTestDispatcher()
    private val unexpectedLogout: suspend () -> LogoutResult = { error("Logout inesperado") }

    @Before fun setUp() = Dispatchers.setMain(dispatcher)
    @After fun tearDown() = Dispatchers.resetMain()

    @Test
    fun invalidFieldsKeepRequestLocal() {
        val model = LoginViewModel({ _, _ -> error("Petición inesperada") }, unexpectedLogout, false)
        model.submit("correo", "test")
        assertEquals(LoginStatus.InvalidEmail, model.status.value)
        model.clearError()
        assertEquals(LoginStatus.Ready, model.status.value)
        model.submit("user@example.invalid", "")
        assertEquals(LoginStatus.EmptyPassword, model.status.value)
    }

    @Test
    fun duplicateSubmitSendsOneRequestAndPreservesPasswordSpaces() {
        val response = CompletableDeferred<ApiResult<Unit>>()
        var calls = 0
        val model = LoginViewModel({ email, password ->
            calls++
            assertEquals("user@example.invalid", email)
            assertEquals(" test ", password)
            response.await()
        }, unexpectedLogout, false)
        repeat(2) { model.submit(" user@example.invalid ", " test ") }
        dispatcher.scheduler.runCurrent()
        assertEquals(LoginStatus.Loading, model.status.value)
        assertEquals(1, calls)
        response.complete(ApiResult.Success(Unit))
        dispatcher.scheduler.runCurrent()
        assertEquals(LoginStatus.Authenticated, model.status.value)
    }

    @Test
    fun connectionFailureAndInvalidCredentialsHaveDifferentStates() {
        var response: ApiResult<Unit> = ApiResult.ConnectionError
        val model = LoginViewModel({ _, _ -> response }, unexpectedLogout, false)
        model.submit("user@example.invalid", "test")
        dispatcher.scheduler.runCurrent()
        assertEquals(LoginStatus.Offline, model.status.value)
        response = ApiResult.HttpError(401, "INVALID_CREDENTIALS")
        model.submit("user@example.invalid", "test")
        dispatcher.scheduler.runCurrent()
        assertEquals(LoginStatus.Rejected, model.status.value)
    }

    @Test
    fun existingSessionOpensSuccessState() {
        val model = LoginViewModel({ _, _ -> error("Petición inesperada") }, unexpectedLogout, true)
        assertEquals(LoginStatus.Authenticated, model.status.value)
    }

    @Test
    fun signOutRunsOnceAndDistinguishesLocalFromConfirmedLogout() {
        for ((result, expected) in listOf(
            LogoutResult.Revoked to LoginStatus.SignedOut,
            LogoutResult.LocalOnly to LoginStatus.SignedOutLocally
        )) {
            var calls = 0
            val model = LoginViewModel({ _, _ -> error("Login inesperado") }, { calls++; result }, true)
            repeat(2) { model.signOut() }
            assertEquals(LoginStatus.SigningOut, model.status.value)
            dispatcher.scheduler.runCurrent()
            assertEquals(1, calls)
            assertEquals(expected, model.status.value)
        }
    }

    @Test
    fun expiredSessionReturnsToFormAndAllowsLoginAgain() {
        val model = LoginViewModel({ _, _ -> ApiResult.Success(Unit) }, unexpectedLogout, true)
        model.onSessionExpired()
        assertEquals(LoginStatus.SessionExpired, model.status.value)
        model.submit("user@example.invalid", "test")
        dispatcher.scheduler.runCurrent()
        assertEquals(LoginStatus.Authenticated, model.status.value)
    }
}
