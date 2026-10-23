package com.securebrowser.platform.auth;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.bind.DefaultValue;

/** Conserva la duración absoluta de los grupos de login, en segundos. */
@ConfigurationProperties("sbp.auth")
public record AuthProperties(@DefaultValue("604800") long refreshTokenLifetimeSeconds) {

    /** Aplica siete días por defecto y exige una duración positiva. */
    public AuthProperties {
        if (refreshTokenLifetimeSeconds <= 0) {
            throw new IllegalArgumentException("Refresh token lifetime must be positive");
        }
    }
}
