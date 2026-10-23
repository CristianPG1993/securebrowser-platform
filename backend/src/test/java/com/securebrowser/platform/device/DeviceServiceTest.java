package com.securebrowser.platform.device;

import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.*;
import java.time.Clock;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;
import org.junit.jupiter.api.Test;
import org.springframework.data.domain.Page;
import org.springframework.data.domain.PageRequest;
import org.springframework.security.access.AccessDeniedException;

/** Comprueba los límites de Service en invocaciones directas, sin los filtros HTTP. */
class DeviceServiceTest {
    private final DeviceRepository repository = mock(DeviceRepository.class);
    private final UserRepository users = mock(UserRepository.class);
    private final Clock clock = mock(Clock.class);
    private final DeviceService service = new DeviceService(repository, users, clock);
    private final AuthenticatedUser user = new AuthenticatedUser(2L, 10L, UserRole.USER);
    private final AuthenticatedUser admin = new AuthenticatedUser(1L, 10L, UserRole.ADMIN);

    @Test
    void userCannotOmitOwnershipFilter() {
        when(repository.findByCompanyWithFilters(10L, 2L, null, PageRequest.of(0, 20)))
                .thenReturn(Page.empty(PageRequest.of(0, 20)));
        service.list(user, 0, 20, null, null);
        verify(repository).findByCompanyWithFilters(10L, 2L, null, PageRequest.of(0, 20));
        verifyNoInteractions(users, clock);
    }

    @Test
    void adminMayListWholeCompanyWithoutSimulatingActivity() {
        when(repository.findByCompanyWithFilters(10L, null, true, PageRequest.of(0, 20)))
                .thenReturn(Page.empty(PageRequest.of(0, 20)));
        service.list(admin, 0, 20, null, true);
        verify(repository).findByCompanyWithFilters(10L, null, true, PageRequest.of(0, 20));
        verifyNoInteractions(users, clock);
    }

    @Test
    void rejectsOtherOwnerBeforeResolvingItsExistence() {
        // USER no puede convertir un filtro ajeno en una consulta administrativa.
        assertThatThrownBy(() -> service.list(user, 0, 20, 3L, null)).isInstanceOf(DeviceNotFoundException.class);
        verifyNoInteractions(repository, users, clock);
    }

    @Test
    void validatesIdentityAndArgumentsBeforeQueries() {
        // Una invocación sin identidad no puede listar dispositivos.
        assertThatThrownBy(() -> service.list(null, 0, 20, null, null)).isInstanceOf(AccessDeniedException.class);
        // Tampoco puede consultar una política.
        assertThatThrownBy(() -> service.getPolicy(null, 1L)).isInstanceOf(AccessDeniedException.class);
        // La paginación sigue validándose fuera del Controller.
        assertThatThrownBy(() -> service.list(admin, -1, 20, null, null)).isInstanceOf(InvalidDeviceRequestException.class);
        // El tamaño máximo no se puede eludir llamando al servicio directamente.
        assertThatThrownBy(() -> service.list(user, 0, 101, null, null)).isInstanceOf(InvalidDeviceRequestException.class);
        // Los filtros de referencia exigen IDs positivos.
        assertThatThrownBy(() -> service.list(admin, 0, 20, 0L, null)).isInstanceOf(InvalidDeviceRequestException.class);
        // Un ID inválido de instalación se rechaza antes de adquirir un bloqueo.
        assertThatThrownBy(() -> service.getPolicy(user, 0L)).isInstanceOf(InvalidDeviceRequestException.class);
        verifyNoInteractions(repository, users, clock);
    }
}
