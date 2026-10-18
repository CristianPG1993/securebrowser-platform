package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.util.Base64;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;

/** Comprueba los requisitos de la clave y la carga de la configuración JWT. */
class JwtPropertiesTest {

    // Configuración aislada para probar las propiedades sin arrancar el backend.
    private final ApplicationContextRunner contextRunner =
            new ApplicationContextRunner()
                    .withUserConfiguration(PropertiesConfiguration.class);

    /** Acepta el mínimo de bytes y claves mayores, preparando HmacSHA256. */
    @ParameterizedTest
    @ValueSource(ints = {32, 64})
    void acceptsKeysWithRequiredDecodedLength(int length) {
        JwtProperties properties = new JwtProperties(
                testSecret(length), "sbp-test", "sbp-api-test", 900);

        assertThat(properties.getSecretKey().getEncoded())
                .containsExactly(new byte[length]);
        assertThat(properties.getSecretKey().getAlgorithm())
                .isEqualTo("HmacSHA256");
    }

    /** Rechaza una clave ausente o sin contenido. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" "})
    void rejectsMissingSecret(String secret) {
        assertThatThrownBy(() ->
                new JwtProperties(secret, "sbp-test", "sbp-api-test", 900))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT secret is required");
    }

    /** Rechaza una representación Base64 inválida sin incluirla en el error. */
    @ParameterizedTest
    @ValueSource(strings = {"not-base64", "%%%"})
    void rejectsMalformedBase64(String secret) {
        assertThatThrownBy(() ->
                new JwtProperties(secret, "sbp-test", "sbp-api-test", 900))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT secret must be valid Base64");
    }

    /** Comprueba el tamaño de los bytes originales y no el del texto Base64. */
    @ParameterizedTest
    @ValueSource(ints = {1, 31})
    void rejectsShortDecodedKeys(int length) {
        assertThatThrownBy(() ->
                new JwtProperties(
                        testSecret(length), "sbp-test", "sbp-api-test", 900))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT secret must contain at least 32 bytes");
    }

    /** Exige un emisor configurado. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" "})
    void rejectsMissingIssuer(String issuer) {
        assertThatThrownBy(() ->
                new JwtProperties(testSecret(32), issuer, "sbp-api-test", 900))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT issuer is required");
    }

    /** Exige una audiencia configurada. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" "})
    void rejectsMissingAudience(String audience) {
        assertThatThrownBy(() ->
                new JwtProperties(testSecret(32), "sbp-test", audience, 900))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT audience is required");
    }

    /** Rechaza duraciones nulas o negativas. */
    @ParameterizedTest
    @ValueSource(longs = {-1L, 0L})
    void rejectsNonPositiveLifetime(long lifetime) {
        assertThatThrownBy(() ->
                new JwtProperties(
                        testSecret(32), "sbp-test", "sbp-api-test", lifetime))
                .isInstanceOf(IllegalArgumentException.class)
                .hasMessage("JWT maximum lifetime must be positive");
    }

    /** Spring enlaza el prefijo configurado y aplica los 900 segundos iniciales. */
    @Test
    void bindsRequiredPropertiesAndDefaultLifetime() {
        contextRunner.withPropertyValues(
                "sbp.security.jwt.secret-base64=" + testSecret(32),
                "sbp.security.jwt.issuer=sbp-test",
                "sbp.security.jwt.audience=sbp-api-test")
                .run(context -> {
                    assertThat(context).hasNotFailed();
                    JwtProperties properties = context.getBean(JwtProperties.class);

                    assertThat(properties.getIssuer()).isEqualTo("sbp-test");
                    assertThat(properties.getAudience()).isEqualTo("sbp-api-test");
                    assertThat(properties.getMaxLifetimeSeconds()).isEqualTo(900);
                    assertThat(properties.getSecretKey().getEncoded())
                            .containsExactly(new byte[32]);
                });
    }

    /** Permite configurar una duración distinta de la inicial. */
    @Test
    void bindsConfiguredLifetime() {
        contextRunner.withPropertyValues(
                "sbp.security.jwt.secret-base64=" + testSecret(32),
                "sbp.security.jwt.issuer=sbp-test",
                "sbp.security.jwt.audience=sbp-api-test",
                "sbp.security.jwt.max-lifetime-seconds=600")
                .run(context -> {
                    assertThat(context).hasNotFailed();
                    assertThat(context.getBean(JwtProperties.class)
                            .getMaxLifetimeSeconds()).isEqualTo(600);
                });
    }

    /** El contexto no arranca si falta una de las propiedades obligatorias. */
    @ParameterizedTest
    @ValueSource(strings = {"secret-base64", "issuer", "audience"})
    void rejectsMissingRequiredPropertyDuringBinding(String missingProperty) {
        String[] properties = {
                "sbp.security.jwt.secret-base64=" + testSecret(32),
                "sbp.security.jwt.issuer=sbp-test",
                "sbp.security.jwt.audience=sbp-api-test"
        };

        String[] configuredProperties = java.util.Arrays.stream(properties)
                .filter(property ->
                        !property.startsWith("sbp.security.jwt." + missingProperty + "="))
                .toArray(String[]::new);

        contextRunner.withPropertyValues(configuredProperties)
                .run(context -> assertThat(context).hasFailed());
    }

    /** Genera bytes conocidos exclusivamente para las pruebas de configuración. */
    private static String testSecret(int length) {
        return Base64.getEncoder().encodeToString(new byte[length]);
    }

    /** Registra únicamente la clase de propiedades que se está probando. */
    @TestConfiguration(proxyBeanMethods = false)
    @EnableConfigurationProperties(JwtProperties.class)
    static class PropertiesConfiguration {
    }
}
