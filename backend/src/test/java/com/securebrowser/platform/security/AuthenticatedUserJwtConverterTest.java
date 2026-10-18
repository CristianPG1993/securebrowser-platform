package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.when;

import java.util.stream.Stream;

import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.dao.InvalidDataAccessResourceUsageException;
import org.springframework.dao.QueryTimeoutException;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.transaction.CannotCreateTransactionException;

/** Comprueba que la autenticación utiliza la identidad actual y clasifica sus fallos. */
class AuthenticatedUserJwtConverterTest {

    // Aísla la conversión de las consultas a PostgreSQL.
    private final AuthenticatedUserResolver resolver = mock(AuthenticatedUserResolver.class);
    private final AuthenticatedUserJwtConverter converter = new AuthenticatedUserJwtConverter(resolver);

    /** Los permisos y la compañía proceden del resolver, incluso con claims alternativos. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void usesResolvedIdentityInsteadOfJwtPermissions(UserRole role) {
        AuthenticatedUser user = new AuthenticatedUser(25L, 7L, role);
        when(resolver.resolve(25L)).thenReturn(user);
        Jwt jwt = Jwt.withTokenValue("test-token").header("alg", "HS256")
                .subject("25").claim("role", "ADMIN").claim("companyId", 999L).build();

        var authentication = converter.convert(jwt);

        assertThat(authentication.getPrincipal()).isSameAs(user);
        assertThat(authentication.getCredentials()).isNull();
        assertThat(authentication.isAuthenticated()).isTrue();
        assertThat(authentication.getAuthorities()).singleElement().satisfies(authority ->
                assertThat(authority.getAuthority()).isEqualTo("ROLE_" + role.name()));
        verify(resolver).resolve(25L);
    }

    /** No almacena en caché el rol entre dos peticiones con el mismo JWT. */
    @Test
    void resolvesUserOnEveryConversion() {
        when(resolver.resolve(25L)).thenReturn(
                new AuthenticatedUser(25L, 7L, UserRole.ADMIN),
                new AuthenticatedUser(25L, 7L, UserRole.USER));
        Jwt jwt = jwtWithSubject("25");

        var first = converter.convert(jwt);
        var second = converter.convert(jwt);

        assertThat(((AuthenticatedUser) first.getPrincipal()).role()).isEqualTo(UserRole.ADMIN);
        assertThat(((AuthenticatedUser) second.getPrincipal()).role()).isEqualTo(UserRole.USER);
        verify(resolver, times(2)).resolve(25L);
    }

    /** Una identidad inválida no llega a persistencia, incluso al invocar directamente. */
    @ParameterizedTest
    @ValueSource(strings = {"", " ", "0", "-1", "abc", "9223372036854775808"})
    void rejectsInvalidSubject(String subject) {
        assertThatThrownBy(() -> converter.convert(jwtWithSubject(subject)))
                .isInstanceOf(BadCredentialsException.class);
        verifyNoInteractions(resolver);
    }

    /** No admite un JWT ausente ni un subject numérico que deba convertirse a texto. */
    @Test
    void rejectsMissingJwtAndWrongSubjectType() {
        assertThatThrownBy(() -> converter.convert(null))
                .isInstanceOf(BadCredentialsException.class);
        Jwt numericSubject = Jwt.withTokenValue("test-token").header("alg", "HS256")
                .claim("sub", 25L).build();
        assertThatThrownBy(() -> converter.convert(numericSubject))
                .isInstanceOf(BadCredentialsException.class);
        verifyNoInteractions(resolver);
    }

    /** La ausencia del usuario sigue siendo un rechazo de autenticación. */
    @Test
    void preservesUnknownUserRejection() {
        BadCredentialsException failure = new BadCredentialsException("Invalid bearer token");
        when(resolver.resolve(25L)).thenThrow(failure);

        assertThatThrownBy(() -> converter.convert(jwtWithSubject("25"))).isSameAs(failure);
    }

    /** Identifica fallos de disponibilidad sin convertirlos en credenciales incorrectas. */
    @ParameterizedTest
    @MethodSource("availabilityFailures")
    void classifiesDatabaseUnavailability(RuntimeException failure) {
        when(resolver.resolve(25L)).thenThrow(failure);

        assertThatThrownBy(() -> converter.convert(jwtWithSubject("25")))
                .isInstanceOfSatisfying(OAuth2AuthenticationException.class, exception -> {
                    assertThat(exception.getError().getErrorCode())
                            .isEqualTo("temporarily_unavailable");
                    assertThat(exception.getMessage()).isEqualTo("Authentication temporarily unavailable");
                    assertThat(exception.getCause()).isSameAs(failure);
                });
    }

    /** Un error de programación SQL no se etiqueta como indisponibilidad temporal. */
    @Test
    void preservesUnexpectedPersistenceFailure() {
        var failure = new InvalidDataAccessResourceUsageException("Invalid SQL");
        when(resolver.resolve(25L)).thenThrow(failure);

        assertThatThrownBy(() -> converter.convert(jwtWithSubject("25"))).isSameAs(failure);
    }

    /** Construye un JWT de prueba ya decodificado; aquí no se comprueba su firma. */
    private static Jwt jwtWithSubject(String subject) {
        return Jwt.withTokenValue("test-token").header("alg", "HS256").subject(subject).build();
    }

    /** Representa conexión no disponible, tiempo de espera y transacción no iniciable. */
    private static Stream<RuntimeException> availabilityFailures() {
        return Stream.of(
                new DataAccessResourceFailureException("Database unavailable"),
                new QueryTimeoutException("Database timeout"),
                new CannotCreateTransactionException("Cannot open database transaction"));
    }
}
