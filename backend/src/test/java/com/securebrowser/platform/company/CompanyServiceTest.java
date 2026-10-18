package com.securebrowser.platform.company;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import java.util.Optional;

import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.security.access.AccessDeniedException;

/** Comprueba las decisiones del servicio con dependencias controladas. */
class CompanyServiceTest {

    /** Un ADMIN obtiene su compañía mediante la consulta por su identificador. */
    @Test
    void returnsOwnCompanyForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = mock(Company.class);
        AuthenticatedUser admin = new AuthenticatedUser(25L, 1L, UserRole.ADMIN);
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
        AuthenticatedUser user = new AuthenticatedUser(25L, 1L, UserRole.USER);

        assertThatThrownBy(() -> service.getOwnCompany(user))
                .isInstanceOf(AccessDeniedException.class);

        verifyNoInteractions(repository);
    }

    /** Lanza el error específico cuando no existe la compañía del ADMIN. */
    @Test
    void rejectsMissingCompanyForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        AuthenticatedUser admin = new AuthenticatedUser(25L, 1L, UserRole.ADMIN);
        when(repository.findById(1L)).thenReturn(Optional.empty());

        assertThatThrownBy(() -> service.getOwnCompany(admin))
                .isInstanceOf(CompanyNotFoundException.class);

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }

    /** Rechaza consultas y cambios sin principal antes de acceder al repositorio. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void rejectsMissingAuthenticatedUser(boolean changingName) {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);

        assertThatThrownBy(() -> {
            if (changingName) {
                service.changeOwnCompanyName(null, "Empresa B");
            } else {
                service.getOwnCompany(null);
            }
        })
                .isInstanceOf(IllegalArgumentException.class);

        verifyNoInteractions(repository);
    }

    /** Cambia y guarda el nombre normalizado de la compañía del ADMIN. */
    @Test
    void changesOwnCompanyNameForAdmin() {
        CompanyRepository repository = mock(CompanyRepository.class);
        CompanyService service = new CompanyService(repository);
        Company company = new Company("Empresa A");
        AuthenticatedUser admin = new AuthenticatedUser(25L, 1L, UserRole.ADMIN);
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
        AuthenticatedUser user = new AuthenticatedUser(25L, 1L, UserRole.USER);

        assertThatThrownBy(() -> service.changeOwnCompanyName(user, "Empresa B"))
                .isInstanceOf(AccessDeniedException.class);

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
        Company company = new Company("Empresa A");
        AuthenticatedUser admin = new AuthenticatedUser(25L, 1L, UserRole.ADMIN);
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
        AuthenticatedUser admin = new AuthenticatedUser(25L, 1L, UserRole.ADMIN);
        when(repository.findById(1L)).thenReturn(Optional.empty());

        assertThatThrownBy(() -> service.changeOwnCompanyName(admin, "Empresa B"))
                .isInstanceOf(CompanyNotFoundException.class);

        verify(repository).findById(1L);
        verifyNoMoreInteractions(repository);
    }
}
