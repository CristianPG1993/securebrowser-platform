package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.security.crypto.password.PasswordEncoder;

/** Comprueba el bean de contraseñas con BCrypt real y sin base de datos. */
class PasswordConfigurationTest {

    // Carga únicamente la configuración que queremos comprobar.
    private final ApplicationContextRunner contextRunner = new ApplicationContextRunner()
            .withUserConfiguration(PasswordConfiguration.class);

    /** Genera un hash con el coste acordado y comprueba contraseñas reales. */
    @Test
    void providesEncoderThatHashesAndVerifiesPasswords() {
        contextRunner.run(context -> {
            // La configuración debe permitir que Spring arranque este contexto aislado.
            assertThat(context).hasNotFailed();

            // Spring debe ofrecer un único objeto PasswordEncoder para inyectarlo después.
            assertThat(context).hasSingleBean(PasswordEncoder.class);

            PasswordEncoder encoder = context.getBean(PasswordEncoder.class);
            String password = "Demo password 2026!";
            String hash = encoder.encode(password);

            // El resultado debe tener formato BCrypt y utilizar el coste 12 acordado.
            assertThat(hash).matches("\\$2[aby]\\$12\\$[./A-Za-z0-9]{53}");

            // La contraseña original debe coincidir con el hash generado.
            assertThat(encoder.matches(password, hash)).isTrue();

            // Una contraseña distinta debe ser rechazada al comprobar ese mismo hash.
            assertThat(encoder.matches("Otra password 2026!", hash)).isFalse();
        });
    }
}
