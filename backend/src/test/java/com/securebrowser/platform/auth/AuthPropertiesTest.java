package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.context.properties.ConfigurationPropertiesBindException;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;

/** Comprueba el registro y los valores de la configuración de autenticación. */
class AuthPropertiesTest {

    // Carga únicamente la configuración de autenticación, sin base de datos ni servidor HTTP.
    private final ApplicationContextRunner contextRunner = new ApplicationContextRunner()
            .withUserConfiguration(AuthConfiguration.class);

    /** Sin una duración explícita, los grupos de login tienen siete días de vigencia. */
    @Test
    void registersPropertiesWithSevenDayDefaultLifetime() {
        contextRunner.run(context -> {
            // Spring debe arrancar correctamente con la configuración predeterminada.
            assertThat(context).hasNotFailed();
            // La configuración registra un único bean AuthProperties para inyectarlo en los servicios.
            assertThat(context).hasSingleBean(AuthProperties.class);

            AuthProperties properties = context.getBean(AuthProperties.class);

            // Siete días equivalen a 7 × 24 × 60 × 60 = 604800 segundos.
            assertThat(properties.refreshTokenLifetimeSeconds()).isEqualTo(604800L);
        });
    }

    /** Una duración explícita sustituye los siete días predeterminados. */
    @Test
    void bindsConfiguredLifetime() {
        contextRunner.withPropertyValues("sbp.auth.refresh-token-lifetime-seconds=86400")
                .run(context -> {
                    // Spring debe aceptar esta configuración con una duración positiva.
                    assertThat(context).hasNotFailed();

                    AuthProperties properties = context.getBean(AuthProperties.class);

                    // El bean contiene el día configurado, sin sustituirlo por los siete días por defecto.
                    assertThat(properties.refreshTokenLifetimeSeconds()).isEqualTo(86400L);
                });
    }

    /** Un segundo es la menor duración entera positiva admitida por la configuración. */
    @Test
    void acceptsOneSecondLifetime() {
        contextRunner.withPropertyValues("sbp.auth.refresh-token-lifetime-seconds=1")
                .run(context -> {
                    // El límite inferior válido permite arrancar el contexto.
                    assertThat(context).hasNotFailed();
                    // Se conserva el segundo configurado, sin aplicar el valor predeterminado.
                    assertThat(context.getBean(AuthProperties.class).refreshTokenLifetimeSeconds())
                            .isEqualTo(1L);
                });
    }

    /** Una duración cero o negativa impide arrancar con una configuración inválida. */
    @ParameterizedTest
    @ValueSource(longs = { 0, -1 })
    void rejectsNonPositiveLifetimeDuringBinding(long lifetime) {
        contextRunner.withPropertyValues("sbp.auth.refresh-token-lifetime-seconds=" + lifetime)
                .run(context -> {
                    // Spring debe rechazar la configuración en lugar de sustituirla por siete días.
                    assertThat(context).hasFailed();
                    // La causa es la comprobación de duración positiva del constructor de AuthProperties.
                    assertThat(context.getStartupFailure())
                            .hasRootCauseInstanceOf(IllegalArgumentException.class)
                            .hasRootCauseMessage("Refresh token lifetime must be positive");
                });
    }

    /** Rechaza texto, decimales y números que no caben en un long. */
    @ParameterizedTest
    @ValueSource(strings = { "abc", "1.5", "9223372036854775808" })
    void rejectsInvalidLifetimeText(String lifetime) {
        contextRunner.withPropertyValues("sbp.auth.refresh-token-lifetime-seconds=" + lifetime)
                .run(context -> {
                    // El valor no se puede interpretar como una duración entera válida.
                    assertThat(context).hasFailed();
                    // El fallo procede del enlace de las propiedades de configuración.
                    assertThat(context.getStartupFailure())
                            .isInstanceOf(ConfigurationPropertiesBindException.class);
                });
    }
}
