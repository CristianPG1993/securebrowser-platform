package com.securebrowser.platform.android.ui.login

import androidx.annotation.StringRes
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.securebrowser.platform.android.R
import com.securebrowser.platform.android.core.network.ApiResult
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/** Estado observable del formulario, libre de credenciales y tokens. */
internal enum class LoginStatus(@get:StringRes val error: Int? = null) {
    Ready, Loading, Authenticated,
    InvalidEmail(R.string.login_invalid_email),
    EmptyPassword(R.string.login_empty_password),
    Rejected(R.string.login_invalid_credentials),
    Offline(R.string.login_connection_error),
    Failed(R.string.login_failed)
}

/** Valida el formulario y publica el resultado del repositorio para Compose. */
internal class LoginViewModel(
    private val login: suspend (String, String) -> ApiResult<Unit>,
    hasSession: Boolean
) : ViewModel() {
    private val current = MutableStateFlow(if (hasSession) LoginStatus.Authenticated else LoginStatus.Ready)
    val status = current.asStateFlow()

    /** Al editar los campos se retira el error del intento anterior. */
    fun clearError() {
        if (current.value.error != null) current.value = LoginStatus.Ready
    }

    /** Envía como máximo una petición a la vez y conserva la contraseña tal como se escribió. */
    fun submit(email: String, password: String) {
        if (current.value == LoginStatus.Loading || current.value == LoginStatus.Authenticated) return
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
