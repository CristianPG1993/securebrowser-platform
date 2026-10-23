package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
import com.securebrowser.platform.policy.dto.PolicyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRole;
import org.junit.jupiter.api.Test;
import org.springframework.security.access.AccessDeniedException;
import tools.jackson.databind.json.JsonMapper;

/** Verifica los permisos del servicio cuando se invoca fuera del Controller. */
class PolicyServiceTest {
    private final PolicyRepository repository = mock(PolicyRepository.class);
    private final PolicyService service = new PolicyService(repository);
    private final AuthenticatedUser admin = new AuthenticatedUser(1L, 10L, UserRole.ADMIN);
    private final AuthenticatedUser user = new AuthenticatedUser(2L, 10L, UserRole.USER);

    @Test
    void checksRoleBeforeResolvingIdsOrQueries() {
        PolicyUpdateRequest request = JsonMapper.builder().build().readValue("{\"name\":\"Nueva\"}", PolicyUpdateRequest.class);
        // Un USER no puede listar políticas aunque intente usar paginación inválida.
        assertThatThrownBy(() -> service.list(user, -1, 0)).isInstanceOf(AccessDeniedException.class);
        // El rol se comprueba antes de consultar si una política existe.
        assertThatThrownBy(() -> service.get(user, Long.MAX_VALUE)).isInstanceOf(AccessDeniedException.class);
        // También se protege el acceso directo al método de escritura.
        assertThatThrownBy(() -> service.update(user, 1L, request)).isInstanceOf(AccessDeniedException.class);
        // La ausencia de identidad nunca permite realizar una consulta.
        assertThatThrownBy(() -> service.get(null, 1L)).isInstanceOf(AccessDeniedException.class);
        verifyNoInteractions(repository);
    }

    @Test
    void rejectsInvalidArgumentsBeforeRepository() {
        // Un ID no positivo no se transforma en una consulta de recurso inexistente.
        assertThatThrownBy(() -> service.get(admin, 0L)).isInstanceOf(InvalidPolicyRequestException.class);
        // Una página negativa se rechaza también fuera de HTTP.
        assertThatThrownBy(() -> service.list(admin, -1, 20)).isInstanceOf(InvalidPolicyRequestException.class);
        // El tamaño máximo del contrato se conserva en Service.
        assertThatThrownBy(() -> service.list(admin, 0, 101)).isInstanceOf(InvalidPolicyRequestException.class);
        // Un PATCH nulo no se procesa desde una invocación directa.
        assertThatThrownBy(() -> service.update(admin, 1L, null)).isInstanceOf(InvalidPolicyRequestException.class);
        verifyNoInteractions(repository);
    }
}
