package com.securebrowser.platform.user;

import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.dto.UserResponse;
import org.springframework.http.MediaType;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

@RestController
@RequestMapping("/api/v1/users/me")
public class UserController {
    private final UserService service;

    public UserController(UserService service) {
        this.service = service;
    }

    @GetMapping(produces = MediaType.APPLICATION_JSON_VALUE)
    public UserResponse getOwnProfile(
            @AuthenticationPrincipal(errorOnInvalidType = true) AuthenticatedUser principal) {
        return service.getOwnProfile(principal);
    }
}
