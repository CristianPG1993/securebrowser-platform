package com.securebrowser.platform.device;

import java.time.Clock;
import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.device.dto.DeviceResponse;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;
import org.springframework.data.domain.PageRequest;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Isolation;
import org.springframework.transaction.annotation.Transactional;

/** Aplica el ámbito de Company, propiedad de instalación y comunicaciones autenticadas. */
@Service
public class DeviceService {
    private final DeviceRepository repository;
    private final UserRepository users;
    private final Clock clock;

    public DeviceService(DeviceRepository repository, UserRepository users, Clock clock) {
        this.repository = repository;
        this.users = users;
        this.clock = clock;
    }

    @Transactional(readOnly = true, isolation = Isolation.REPEATABLE_READ)
    public PageResponse<DeviceResponse> list(AuthenticatedUser actor, int page, int size,
            Long userId, Boolean active) {
        requireIdentity(actor);
        if (page < 0 || size < 1 || size > 100) throw new InvalidDeviceRequestException();
        if (userId != null) {
            requireId(userId);
            if (actor.role() == UserRole.USER && !userId.equals(actor.userId())) {
                throw new DeviceNotFoundException();
            }
            users.findByIdAndCompany_Id(userId, actor.companyId()).orElseThrow(DeviceNotFoundException::new);
        }
        // USER nunca puede retirar el filtro de propietario omitiendo userId.
        Long owner = actor.role() == UserRole.USER ? actor.userId() : userId;
        return PageResponse.from(repository.findByCompanyWithFilters(actor.companyId(), owner, active,
                PageRequest.of(page, size)).map(DeviceResponse::from));
    }

    /** El snapshot de Policy y el registro de la comunicación pertenecen a la misma transacción. */
    @Transactional(isolation = Isolation.REPEATABLE_READ)
    public PolicyResponse getPolicy(AuthenticatedUser actor, Long deviceId) {
        requireIdentity(actor);
        requireId(deviceId);
        // La propiedad se exige también al ADMIN; el catálogo administrativo usa /policies.
        Device device = repository.findByIdAndCompany_IdAndUser_Id(deviceId, actor.companyId(), actor.userId())
                .orElseThrow(DeviceNotFoundException::new);
        if (!device.isActive()) throw new DeviceInactiveException();
        PolicyResponse response = PolicyResponse.from(device.getPolicy());
        device.recordLastSeen(clock.instant());
        repository.flush();
        return response;
    }

    private static void requireIdentity(AuthenticatedUser actor) {
        if (actor == null) throw new AccessDeniedException("Authenticated identity is required");
    }

    private static void requireId(Long id) {
        if (id == null || id <= 0) throw new InvalidDeviceRequestException();
    }
}
