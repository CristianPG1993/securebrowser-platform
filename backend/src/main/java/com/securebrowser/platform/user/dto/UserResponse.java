package com.securebrowser.platform.user.dto;

import java.time.Instant;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

/** Perfil público del usuario; no incluye contraseñas ni hashes. */
public record UserResponse(Long id, Long companyId, String name, String lastName, String email,
        UserRole role, Instant createdAt, Instant updatedAt) {
    public static UserResponse from(User user) {
        return new UserResponse(user.getId(), user.getCompany().getId(), user.getName(), user.getLastName(),
                user.getEmail(), user.getRole(), user.getCreatedAt(), user.getUpdatedAt());
    }
}
