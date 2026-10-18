package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import java.util.Optional;

import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.NullSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.security.authentication.BadCredentialsException;

/** Comprueba la resolución de identidad con dependencias controladas. */
class AuthenticatedUserResolverTest {

    private final UserRepository repository = mock(UserRepository.class);
    private final AuthenticatedUserResolver resolver =
            new AuthenticatedUserResolver(repository);

    /** Conserva la identidad, compañía y rol obtenidos del repositorio. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void resolvesIdentityAndCurrentRole(UserRole role) {
        User user = storedUser(role);
        when(repository.findById(1L)).thenReturn(Optional.of(user));

        AuthenticatedUser result = resolver.resolve(1L);

        assertThat(result.userId()).isEqualTo(1L);
        assertThat(result.companyId()).isEqualTo(7L);
        assertThat(result.role()).isEqualTo(role);
        assertThat(result.getName()).isEqualTo("1");
        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Rechaza identificadores inválidos antes de consultar persistencia. */
    @ParameterizedTest
    @NullSource
    @ValueSource(longs = {Long.MIN_VALUE, -1L, 0L})
    void rejectsInvalidIdWithoutQueryingRepository(Long userId) {
        assertThatThrownBy(() -> resolver.resolve(userId))
                .isInstanceOf(BadCredentialsException.class)
                .hasMessage("Invalid bearer token");

        verifyNoInteractions(repository);
    }

    /** Rechaza usuarios inexistentes con un mensaje genérico. */
    @Test
    void rejectsUnknownUser() {
        when(repository.findById(1L)).thenReturn(Optional.empty());

        assertThatThrownBy(() -> resolver.resolve(1L))
                .isInstanceOf(BadCredentialsException.class)
                .hasMessage("Invalid bearer token");

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Consulta el repositorio en cada llamada y refleja el rol actualizado. */
    @Test
    void resolvesCurrentRoleOnEveryCall() {
        User user = storedUser(UserRole.ADMIN);
        when(repository.findById(1L)).thenReturn(Optional.of(user));

        AuthenticatedUser before = resolver.resolve(1L);
        when(user.getRole()).thenReturn(UserRole.USER);
        AuthenticatedUser after = resolver.resolve(1L);

        assertThat(before.role()).isEqualTo(UserRole.ADMIN);
        assertThat(after.role()).isEqualTo(UserRole.USER);
        verify(repository, times(2)).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Conserva el fallo de persistencia para distinguirlo de un JWT inválido. */
    @Test
    void propagatesDatabaseFailureWithoutTreatingItAsInvalidCredentials() {
        DataAccessResourceFailureException failure =
                new DataAccessResourceFailureException("Database unavailable");
        when(repository.findById(1L)).thenThrow(failure);

        assertThatThrownBy(() -> resolver.resolve(1L)).isSameAs(failure);

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Prepara un usuario y su compañía con identificadores controlados. */
    private User storedUser(UserRole role) {
        Company company = mock(Company.class);
        when(company.getId()).thenReturn(7L);

        User user = mock(User.class);
        when(user.getId()).thenReturn(1L);
        when(user.getCompany()).thenReturn(company);
        when(user.getRole()).thenReturn(role);
        return user;
    }
}
