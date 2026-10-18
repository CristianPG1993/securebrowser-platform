package com.securebrowser.platform.security;

import java.util.List;
import java.util.Objects;

import org.springframework.security.authentication.AbstractAuthenticationToken;
import org.springframework.security.core.authority.SimpleGrantedAuthority;

/** Representa en Spring Security un usuario cuya autenticación ya se ha verificado. */
public final class AuthenticatedUserAuthenticationToken extends AbstractAuthenticationToken {

    // Identificador de versión requerido por la clase base serializable.
    private static final long serialVersionUID = 1L;

    // Identidad, compañía y rol obtenidos de PostgreSQL para la petición actual.
    private final AuthenticatedUser principal;

    /** Recibe el usuario resuelto después de validar el JWT y asigna su rol actual. */
    public AuthenticatedUserAuthenticationToken(AuthenticatedUser principal) {
        super(List.of(new SimpleGrantedAuthority("ROLE_"
                + Objects.requireNonNull(principal, "Authenticated user is required")
                        .role().name())));
        this.principal = principal;
        super.setAuthenticated(true);
    }

    /** Entrega la identidad que utilizarán los controles de acceso y controllers. */
    @Override
    public AuthenticatedUser getPrincipal() {
        return principal;
    }

    /** No conserva contraseñas ni el JWT original como credenciales. */
    @Override
    public Object getCredentials() {
        return null;
    }

    /** Permite retirar la autenticación; para concederla debe usarse el constructor. */
    @Override
    public void setAuthenticated(boolean authenticated) {
        if (authenticated) {
            throw new IllegalArgumentException("Use the authenticated user constructor");
        }
        super.setAuthenticated(false);
    }
}
