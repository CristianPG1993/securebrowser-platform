package com.securebrowser.platform.security;

import java.util.Objects;

import org.springframework.core.convert.converter.Converter;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.dao.TransientDataAccessException;
import org.springframework.security.authentication.AbstractAuthenticationToken;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.oauth2.core.OAuth2Error;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.stereotype.Component;
import org.springframework.transaction.CannotCreateTransactionException;

/** Convierte un JWT ya validado en la identidad actual obtenida de PostgreSQL. */
@Component
public final class AuthenticatedUserJwtConverter
        implements Converter<Jwt, AbstractAuthenticationToken> {

    // Distingue la indisponibilidad de persistencia de las credenciales inválidas.
    static final String TEMPORARILY_UNAVAILABLE = "temporarily_unavailable";

    // Consulta al usuario en cada petición, sin reutilizar permisos del JWT.
    private final AuthenticatedUserResolver resolver;

    /** Recibe el servicio transaccional de resolución de identidad. */
    public AuthenticatedUserJwtConverter(AuthenticatedUserResolver resolver) {
        this.resolver = Objects.requireNonNull(resolver, "User resolver is required");
    }

    /** Utiliza únicamente sub; ignora cualquier rol o compañía incluidos en el JWT. */
    @Override
    public AbstractAuthenticationToken convert(Jwt jwt) {
        Long userId = readUserId(jwt);
        try {
            return new AuthenticatedUserAuthenticationToken(resolver.resolve(userId));
        } catch (TransientDataAccessException | DataAccessResourceFailureException
                | CannotCreateTransactionException exception) {
            throw new OAuth2AuthenticationException(
                    new OAuth2Error(TEMPORARILY_UNAVAILABLE),
                    "Authentication temporarily unavailable", exception);
        }
    }

    /** Mantiene el rechazo de identidad incluso ante una invocación directa inválida. */
    private static Long readUserId(Jwt jwt) {
        if (jwt == null || !(jwt.getClaims().get("sub") instanceof String subject)) {
            throw new BadCredentialsException("Invalid bearer token");
        }
        try {
            long userId = Long.parseLong(subject);
            if (userId <= 0) {
                throw new BadCredentialsException("Invalid bearer token");
            }
            return userId;
        } catch (NumberFormatException exception) {
            throw new BadCredentialsException("Invalid bearer token");
        }
    }
}
