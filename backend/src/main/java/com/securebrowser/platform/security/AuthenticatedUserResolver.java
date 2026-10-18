package com.securebrowser.platform.security;

import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;

import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Obtiene la identidad y los permisos actuales del usuario desde PostgreSQL. */
@Service
public class AuthenticatedUserResolver {

    private final UserRepository userRepository;

    /** Recibe el acceso a persistencia mediante inyección por constructor. */
    public AuthenticatedUserResolver(UserRepository userRepository) {
        this.userRepository = userRepository;
    }

    /** Resuelve el usuario indicado por un JWT previamente validado. */
    @Transactional(readOnly = true)
    public AuthenticatedUser resolve(Long userId) {
        if (userId == null || userId <= 0) {
            throw new BadCredentialsException("Invalid bearer token");
        }

        User user = userRepository.findById(userId)
                .orElseThrow(() -> new BadCredentialsException("Invalid bearer token"));

        return new AuthenticatedUser(
                user.getId(),
                user.getCompany().getId(),
                user.getRole());
    }
}
