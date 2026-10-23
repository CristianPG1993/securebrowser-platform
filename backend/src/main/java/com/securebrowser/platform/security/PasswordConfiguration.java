package com.securebrowser.platform.security;

import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;
import org.springframework.security.crypto.password.PasswordEncoder;

/** Configura el tratamiento de contraseñas acordado para el backend. */
@Configuration(proxyBeanMethods = false)
public class PasswordConfiguration {

    /** Proporciona BCrypt con el coste inicial 12 definido en la arquitectura. */
    @Bean
    public PasswordEncoder passwordEncoder() {
        return new BCryptPasswordEncoder(12);
    }
}
