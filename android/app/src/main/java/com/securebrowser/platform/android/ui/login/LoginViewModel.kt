package com.securebrowser.platform.android.ui.login

import androidx.annotation.StringRes
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.core.network.ApiResult
import com.securebrowser.platform.android.data.repository.LogoutResult
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/** Estado observable del formulario, libre de credenciales y tokens. */
internal enum class LoginStatus(@get:StringRes val error: Int? = null) {
    Ready, Loading, Authenticated, SigningOut, SignedOut, SignedOutLocally,
    InvalidEmail(R.string.login_invalid_email),
    EmptyPassword(R.string.login_empty_password),
    Rejected(R.string.login_invalid_credentials),
    Offline(R.string.login_connection_error),
    SessionExpired(R.string.login_session_expired),
    Failed(R.string.login_failed)
}

/** Valida el formulario y publica el resultado del repositorio para Compose. */
internal class LoginViewModel(
    private val login: suspend (String, String) -> ApiResult<Unit>,
    private val logout: suspend () -> LogoutResult,
    private val expireIfUnauthorized: suspend () -> Boolean,
    hasSession: Boolean
) : ViewModel() {
    private val current = MutableStateFlow(if (hasSession) LoginStatus.Authenticated else LoginStatus.Ready)
    val status = current.asStateFlow()

    /** Devuelve al formulario solo si sigue mostrándose la sesión autenticada. */
    suspend fun checkSession() {
        if (current.value == LoginStatus.Authenticated && expireIfUnauthorized()) {
            current.compareAndSet(LoginStatus.Authenticated, LoginStatus.SessionExpired)
        }
    }

    /** Al editar los campos se retira el error del intento anterior. */
    fun clearError() {
        if (current.value.error != null || current.value == LoginStatus.SignedOut ||
            current.value == LoginStatus.SignedOutLocally) current.value = LoginStatus.Ready
    }

    /** Retira la vista privada al pulsar el botón y comunica cómo terminó el cierre. */
    fun signOut() {
        if (current.value != LoginStatus.Authenticated) return
        current.value = LoginStatus.SigningOut
        viewModelScope.launch {
            current.value = when (logout()) {
                LogoutResult.Revoked -> LoginStatus.SignedOut
                LogoutResult.LocalOnly -> LoginStatus.SignedOutLocally
            }
        }
    }

    /** Envía como máximo una petición a la vez y conserva la contraseña tal como se escribió. */
    fun submit(email: String, password: String) {
        if (current.value in setOf(LoginStatus.Loading, LoginStatus.Authenticated, LoginStatus.SigningOut)) return
        val address = email.trim()
        if (!emailPattern.matches(address)) {
            current.value = LoginStatus.InvalidEmail
            return
        }
        if (password.isEmpty()) {
            current.value = LoginStatus.EmptyPassword
            return
        }
        current.value = LoginStatus.Loading
        viewModelScope.launch {
            try {
                current.value = when (val result = login(address, password)) {
                    is ApiResult.Success -> LoginStatus.Authenticated
                    is ApiResult.HttpError -> if (result.status == 401 && result.code == "INVALID_CREDENTIALS") {
                        LoginStatus.Rejected
                    } else {
                        LoginStatus.Failed
                    }
                    ApiResult.ConnectionError -> LoginStatus.Offline
                    ApiResult.InvalidResponse -> LoginStatus.Failed
                }
            } finally {
                if (current.value == LoginStatus.Loading) current.value = LoginStatus.Ready
            }
        }
    }

    private companion object {
        val emailPattern = Regex("""[^\s@]+@[^\s@]+\.[^\s@]+""")
    }
}
