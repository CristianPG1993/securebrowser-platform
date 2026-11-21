package com.securebrowser.platform.android.ui.login

import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.junit4.StateRestorationTester
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performTextInput
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

/** Verifica que Android no recupera las credenciales al restaurar el formulario. */
class LoginScreenTest {
    @get:Rule val compose = createComposeRule()

    @Test
    fun credentialsAreClearedOnStateRestoration() {
        val restoration = StateRestorationTester(compose)
        restoration.setContent { LoginScreen(LoginStatus.Ready, { _, _ -> }, {}, {}) }
        compose.onNodeWithText("Correo electrónico").performTextInput("user@example.invalid")
        compose.onNodeWithText("Contraseña").performTextInput("test-only")
        restoration.emulateSavedInstanceStateRestore()
        assertEquals("", compose.onNodeWithText("Correo electrónico").fetchSemanticsNode().config[SemanticsProperties.EditableText].text)
        assertEquals("", compose.onNodeWithText("Contraseña").fetchSemanticsNode().config[SemanticsProperties.EditableText].text)
    }
}
