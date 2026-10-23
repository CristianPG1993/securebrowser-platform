package com.securebrowser.platform.user;

import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.dto.UserResponse;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Implementa exclusivamente la consulta del perfil propio prevista en el bloque de autenticación. */
@Service
public class UserService {
    private final UserRepository repository;

    public UserService(UserRepository repository) {
        this.repository = repository;
    }

    @Transactional(readOnly = true)
    public UserResponse getOwnProfile(AuthenticatedUser principal) {
        if (principal == null) {
            throw new BadCredentialsException("Invalid authenticated identity");
        }
        User user = repository.findByIdAndCompany_Id(principal.userId(), principal.companyId())
                .orElseThrow(() -> new BadCredentialsException("Invalid authenticated identity"));
        return UserResponse.from(user);
    }
}
