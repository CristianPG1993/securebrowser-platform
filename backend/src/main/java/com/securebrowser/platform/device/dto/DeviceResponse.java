package com.securebrowser.platform.device.dto;

import java.time.Instant;
import com.securebrowser.platform.device.Device;

/** Expone solo los doce campos de DeviceResponse del contrato. */
public record DeviceResponse(Long id, String deviceIdentifier, String name, Long companyId,
        Long userId, Long licenseId, Long policyId, Long enrollmentTokenId, boolean active,
        Instant createdAt, Instant updatedAt, Instant lastSeenAt) {
    public static DeviceResponse from(Device device) {
        return new DeviceResponse(device.getId(), device.getDeviceIdentifier(), device.getName(),
                device.getCompany().getId(), device.getUser().getId(), device.getLicense().getId(),
                device.getPolicy().getId(), device.getEnrollmentToken().getId(), device.isActive(),
                device.getCreatedAt(), device.getUpdatedAt(), device.getLastSeenAt());
    }
}
