package com.securebrowser.platform.company;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import java.util.Optional;

import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba las decisiones del servicio con dependencias controladas. */
class CompanyServiceTest {

    /** Un ADMIN obtiene su compañía mediante la consulta por su identificador. */
    @Test
    void returnsOwnCompanyForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = mock(Company.class);
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(company);
        when(company.getId()).thenReturn(1L);
        when(repository.findById(1L)).thenReturn(Optional.of(company));

        Company result = service.getOwnCompany(admin);

        assertThat(result).isSameAs(company);
        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Rechaza el acceso USER antes de consultar compañías. */
    @Test
    void rejectsUserBeforeQueryingRepository() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        User user = mock(User.class);

        when(user.getRole()).thenReturn(UserRole.USER);

        assertThatThrownBy(() -> service.getOwnCompany(user))
                .isInstanceOf(SecurityException.class);

        verifyNoInteractions(repository);
    }

    /** Lanza el error específico cuando no existe la compañía del ADMIN. */
    @Test
    void rejectsMissingCompanyForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = mock(Company.class);
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(company);
        when(company.getId()).thenReturn(1L);
        when(repository.findById(1L)).thenReturn(Optional.empty());

        assertThatThrownBy(() -> service.getOwnCompany(admin))
                .isInstanceOf(CompanyNotFoundException.class);

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Rechaza una llamada sin usuario antes de acceder al repositorio. */
    @Test
    void rejectsMissingAuthenticatedUser() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);

        assertThatThrownBy(() -> service.getOwnCompany(null))
                .isInstanceOf(IllegalArgumentException.class);

        verifyNoInteractions(repository);
    }

    /** Rechaza un contexto sin compañía persistida antes de consultar datos. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void rejectsMissingOrUnpersistedCompany(boolean hasCompany) {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(
                hasCompany ? new Company("Empresa A") : null);

        assertThatThrownBy(() -> service.getOwnCompany(admin))
                .isInstanceOf(IllegalArgumentException.class);

        verifyNoInteractions(repository);
    }

    /** Cambia y guarda el nombre normalizado de la compañía del ADMIN. */
    @Test
    void changesOwnCompanyNameForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = spy(new Company("Empresa A"));
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(company);
        when(company.getId()).thenReturn(1L);
        when(repository.findById(1L)).thenReturn(Optional.of(company));
        when(repository.save(company)).thenReturn(company);

        Company result = service.changeOwnCompanyName(admin, "  Empresa B  ");

        assertThat(result).isSameAs(company);
        assertThat(result.getName()).isEqualTo("Empresa B");
        verify(repository).findById(1L);
        verify(repository).save(company);
        verifyNoMoreInteractions(repository);
    }

    /**
     * Rechaza el cambio de nombre solicitado por USER sin acceder al repositorio.
     */
    @Test
    void rejectsNameChangeForUser() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        User user = mock(User.class);

        when(user.getRole()).thenReturn(UserRole.USER);

        assertThatThrownBy(() -> service.changeOwnCompanyName(user, "Empresa B"))
                .isInstanceOf(SecurityException.class);

        verifyNoInteractions(repository);
    }

    /** Rechaza nombres inválidos conservando el anterior y sin guardar cambios. */
    @ParameterizedTest
    @ValueSource(strings = { "null", "blank", "tooLong" })
    void rejectsInvalidNameWithoutSaving(String scenario) {
        String invalidName = switch (scenario) {
            case "null" -> null;
            case "blank" -> "   ";
            case "tooLong" -> "A".repeat(151);
            default -> throw new IllegalArgumentException("Unknown test scenario");
        };

        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = spy(new Company("Empresa A"));
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(company);
        when(company.getId()).thenReturn(1L);
        when(repository.findById(1L)).thenReturn(Optional.of(company));

        assertThatThrownBy(() -> service.changeOwnCompanyName(admin, invalidName))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(company.getName()).isEqualTo("Empresa A");
        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Rechaza la modificación de una compañía no encontrada sin guardar datos. */
    @Test
    void rejectsNameChangeWhenCompanyIsMissing() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = mock(Company.class);
        User admin = mock(User.class);

        when(admin.getRole()).thenReturn(UserRole.ADMIN);
        when(admin.getCompany()).thenReturn(company);
        when(company.getId()).thenReturn(1L);
        when(repository.findById(1L)).thenReturn(Optional.empty());

        assertThatThrownBy(() -> service.changeOwnCompanyName(admin, "Empresa B"))
                .isInstanceOf(CompanyNotFoundException.class);

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }
}