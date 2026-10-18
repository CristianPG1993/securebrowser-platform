package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba la identidad y los permisos entregados a Spring Security. */
class AuthenticatedUserAuthenticationTokenTest {

    /** Conserva el principal y concede únicamente la autoridad de su rol actual. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void exposesAuthenticatedPrincipalAndCurrentRole(UserRole role) {
        AuthenticatedUser principal = new AuthenticatedUser(25L, 7L, role);

        AuthenticatedUserAuthenticationToken authentication =
                new AuthenticatedUserAuthenticationToken(principal);

        assertThat(authentication.isAuthenticated()).isTrue();
        assertThat(authentication.getPrincipal()).isSameAs(principal);
        assertThat(authentication.getName()).isEqualTo("25");
        assertThat(authentication.getCredentials()).isNull();
        assertThat(authentication.getAuthorities()).singleElement()
                .satisfies(authority ->
                        assertThat(authority.getAuthority()).isEqualTo("ROLE_" + role.name()));
    }

    /** No permite construir una autenticación sin identidad. */
    @Test
    void rejectsMissingPrincipal() {
        assertThatThrownBy(() -> new AuthenticatedUserAuthenticationToken(null))
                .isInstanceOf(NullPointerException.class)
                .hasMessage("Authenticated user is required");
    }

    /** Permite retirar la marca de autenticación del objeto. */
    @Test
    void allowsClearingAuthenticatedState() {
        AuthenticatedUserAuthenticationToken authentication =
                new AuthenticatedUserAuthenticationToken(
                        new AuthenticatedUser(25L, 7L, UserRole.ADMIN));

        authentication.setAuthenticated(false);

        assertThat(authentication.isAuthenticated()).isFalse();
    }

    /** El setter no puede conceder autenticación, sea cual sea su estado anterior. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void rejectsSettingAuthenticatedToTrue(boolean initiallyAuthenticated) {
        AuthenticatedUserAuthenticationToken authentication =
                new AuthenticatedUserAuthenticationToken(
                        new AuthenticatedUser(25L, 7L, UserRole.USER));
        if (!initiallyAuthenticated) {
            authentication.setAuthenticated(false);
        }

        assertThatThrownBy(() -> authentication.setAuthenticated(true))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("Use the authenticated user constructor");

        assertThat(authentication.isAuthenticated()).isEqualTo(initiallyAuthenticated);
    }
}
