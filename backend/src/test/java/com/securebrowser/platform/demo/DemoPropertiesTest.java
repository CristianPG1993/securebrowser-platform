package com.securebrowser.platform.demo;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;

class DemoPropertiesTest {
    @ParameterizedTest
    @MethodSource("invalidPasswords")
    void rejectsInvalidInitialPasswords(String password) {
        // El ADMIN requiere una contraseña nueva dentro de los límites acordados.
        assertThatThrownBy(() -> new DemoProperties(password, "User demo 2026!"))
                .isInstanceOf(IllegalArgumentException.class);
        // Las mismas reglas se aplican al USER.
        assertThatThrownBy(() -> new DemoProperties("Admin demo 2026!", password))
                .isInstanceOf(IllegalArgumentException.class);
    }

    @Test
    void validCredentialsAreRedactedAndDemoIsDisabledByDefault() {
        DemoProperties properties = new DemoProperties("a".repeat(12), "á".repeat(36));
        // La representación del objeto nunca incluye sus contraseñas.
        assertThat(properties.toString()).doesNotContain("a".repeat(12), "á".repeat(36));
        new ApplicationContextRunner().withUserConfiguration(DemoConfiguration.class, DemoDataInitializer.class)
                .run(context -> {
                    // Sin el perfil demo no se registra ni se ejecuta el inicializador.
                    assertThat(context).hasNotFailed().doesNotHaveBean(DemoDataInitializer.class)
                            .doesNotHaveBean(DemoProperties.class);
                });
    }

    static Stream<String> invalidPasswords() {
        return Stream.of(null, "", "short", "a".repeat(73), "á".repeat(37));
    }
}
