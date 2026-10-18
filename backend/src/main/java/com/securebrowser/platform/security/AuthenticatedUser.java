package com.securebrowser.platform.security;

import java.security.Principal;

import com.securebrowser.platform.user.UserRole;

/** Conserva la identidad y el ámbito del usuario autenticado en una petición. */
public record AuthenticatedUser(
        Long userId,
        Long companyId,
        UserRole role) implements Principal {

    /** Exige identificadores persistidos positivos y un rol definido. */
    public AuthenticatedUser {
        if (userId == null || userId <= 0) {
            throw new IllegalArgumentException("User id must be positive");
        }
        if (companyId == null || companyId <= 0) {
            throw new IllegalArgumentException("Company id must be positive");
        }
        if (role == null) {
            throw new IllegalArgumentException("User role is required");
        }
    }

    /** Identifica al principal mediante el identificador estable del usuario. */
    @Override
    public String getName() {
        return userId.toString();
    }
}
