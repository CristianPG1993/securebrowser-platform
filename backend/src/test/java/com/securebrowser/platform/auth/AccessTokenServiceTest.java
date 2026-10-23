package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.time.temporal.ChronoUnit;
import java.util.Base64;

import com.securebrowser.platform.security.JwtConfiguration;
import com.securebrowser.platform.security.JwtProperties;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.jwt.JwtEncoder;

/** Comprueba la emisión de tokens con firma real y un reloj fijo, sin base de datos. */
class AccessTokenServiceTest {

    private static final Instant NOW = Instant.parse("2026-10-22T08:00:00Z");

    // Clave conocida de prueba: nunca se utiliza fuera de este contexto de test.
    private static final byte[] TEST_KEY = new byte[32];

    // Carga únicamente el servicio y la configuración JWT que necesita.
    private final ApplicationContextRunner contextRunner = new ApplicationContextRunner()
            .withUserConfiguration(JwtConfiguration.class, AccessTokenService.class)
            .withPropertyValues(
                    "sbp.security.jwt.secret-base64=" + Base64.getEncoder().encodeToString(TEST_KEY),
                    "sbp.security.jwt.issuer=sbp-access-test",
                    "sbp.security.jwt.audience=sbp-api-test",
                    "sbp.security.jwt.max-lifetime-seconds=900")
            .withBean(Clock.class, () -> Clock.fixed(NOW, ZoneOffset.UTC));

    /** Un usuario validado recibe un JWT con los seis claims acordados y 15 minutos de vida. */
    @Test
    void issuesSignedTokenWithConfiguredClaimsAndLifetime() {
        contextRunner.run(context -> {
            // Comprueba que Spring ha creado correctamente las dependencias de esta prueba.
            assertThat(context).hasNotFailed();

            AccessTokenService service = context.getBean(AccessTokenService.class);
            JwtDecoder decoder = context.getBean(JwtDecoder.class);

            // La sesión dura siete días, por lo que permite emitir los 900 segundos completos.
            Jwt issuedToken = service.issue(25L, NOW.plus(7, ChronoUnit.DAYS));
            // El verificador real comprueba la firma y los claims del token emitido.
            Jwt verifiedToken = decoder.decode(issuedToken.getTokenValue());

            // La firma utiliza el algoritmo HS256 acordado.
            assertThat(verifiedToken.getHeaders()).containsEntry("alg", "HS256");
            // El sujeto identifica al usuario 25.
            assertThat(verifiedToken.getSubject()).isEqualTo("25");
            // El emisor procede de las propiedades de este contexto.
            assertThat(verifiedToken.getClaimAsString("iss")).isEqualTo("sbp-access-test");
            // El token está destinado a la audiencia configurada.
            assertThat(verifiedToken.getAudience()).containsExactly("sbp-api-test");
            // La emisión utiliza la hora del reloj fijo.
            assertThat(verifiedToken.getIssuedAt()).isEqualTo(NOW);
            // La caducidad respeta los 900 segundos configurados.
            assertThat(verifiedToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(900));
            // El token incluye un identificador propio no vacío.
            assertThat(verifiedToken.getId()).isNotBlank();
            // Solo se incluyen los seis claims acordados, sin roles ni datos personales.
            assertThat(verifiedToken.getClaims())
                    .containsOnlyKeys("sub", "iss", "aud", "iat", "exp", "jti");
        });
    }

    /** El JWT nunca permite seguir usando una sesión más allá de su caducidad. */
    @Test
    void capsTokenExpiryAtSessionExpiry() {
        contextRunner.run(context -> {
            AccessTokenService service = context.getBean(AccessTokenService.class);
            JwtDecoder decoder = context.getBean(JwtDecoder.class);
            Instant sessionExpiresAt = NOW.plusSeconds(180);

            // Aunque la configuración permite 900 segundos, a esta sesión solo le quedan 180.
            Jwt issuedToken = service.issue(25L, sessionExpiresAt);
            Jwt verifiedToken = decoder.decode(issuedToken.getTokenValue());

            // La emisión parte de la hora actual del reloj fijo.
            assertThat(verifiedToken.getIssuedAt()).isEqualTo(NOW);
            // El JWT caduca junto con la sesión: a las 08:03, en lugar de a las 08:15.
            assertThat(verifiedToken.getExpiresAt()).isEqualTo(sessionExpiresAt);
        });
    }

    /** La duración procede de la configuración, aunque sea distinta de los 900 segundos habituales. */
    @Test
    void respectsCustomTokenLifetime() {
        contextRunner.withPropertyValues("sbp.security.jwt.max-lifetime-seconds=60")
                .run(context -> {
                    AccessTokenService service = context.getBean(AccessTokenService.class);
                    JwtDecoder decoder = context.getBean(JwtDecoder.class);

                    // La sesión permite siete días, pero esta configuración limita el JWT a un minuto.
                    Jwt issuedToken = service.issue(25L, NOW.plus(7, ChronoUnit.DAYS));
                    Jwt verifiedToken = decoder.decode(issuedToken.getTokenValue());

                    // La emisión sigue utilizando el reloj fijo: las 08:00.
                    assertThat(verifiedToken.getIssuedAt()).isEqualTo(NOW);
                    // La caducidad es a las 08:01, según los 60 segundos configurados para esta prueba.
                    assertThat(verifiedToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(60));
                });
    }

    /** Una sesión que ya ha llegado a su caducidad no puede obtener otro JWT. */
    @ParameterizedTest
    @ValueSource(longs = { -1, 0 })
    void rejectsExpiredSession(long secondsUntilExpiry) {
        contextRunner.run(context -> {
            AccessTokenService service = context.getBean(AccessTokenService.class);
            Instant sessionExpiresAt = NOW.plusSeconds(secondsUntilExpiry);

            // Rechaza tanto una sesión caducada hace un segundo como una que caduca justo ahora.
            assertThatThrownBy(() -> service.issue(25L, sessionExpiresAt))
                    .isInstanceOf(IllegalArgumentException.class)
                    .hasMessage("Session expiry must allow a positive JWT lifetime");
        });
    }

    /** Dos emisiones para el mismo usuario y en el mismo instante producen tokens distintos. */
    @Test
    void generatesDifferentIdentifiersForEachToken() {
        contextRunner.run(context -> {
            AccessTokenService service = context.getBean(AccessTokenService.class);
            JwtDecoder decoder = context.getBean(JwtDecoder.class);
            Instant sessionExpiresAt = NOW.plus(7, ChronoUnit.DAYS);

            // El reloj fijo, el usuario y la caducidad de sesión son iguales en ambas emisiones.
            Jwt firstToken = service.issue(25L, sessionExpiresAt);
            Jwt secondToken = service.issue(25L, sessionExpiresAt);
            Jwt firstVerifiedToken = decoder.decode(firstToken.getTokenValue());
            Jwt secondVerifiedToken = decoder.decode(secondToken.getTokenValue());

            // Cada JWT firmado contiene un jti distinto, aunque los demás datos coincidan.
            assertThat(firstVerifiedToken.getId()).isNotEqualTo(secondVerifiedToken.getId());
            // Los textos completos que se entregarán al cliente también son distintos.
            assertThat(firstToken.getTokenValue()).isNotEqualTo(secondToken.getTokenValue());
        });
    }

    /** Un JWT de acceso siempre debe identificar a un usuario mediante un ID positivo. */
    @ParameterizedTest
    @NullSource
    @ValueSource(longs = { 0, -1 })
    void rejectsInvalidUserId(Long userId) {
        contextRunner.run(context -> {
            AccessTokenService service = context.getBean(AccessTokenService.class);
            Instant sessionExpiresAt = NOW.plus(7, ChronoUnit.DAYS);

            // Rechaza el ID ausente, cero o negativo aunque la sesión tenga tiempo disponible.
            assertThatThrownBy(() -> service.issue(userId, sessionExpiresAt))
                    .isInstanceOf(IllegalArgumentException.class)
                    .hasMessage("A positive user ID is required");
        });
    }

    /** La fecha de caducidad de la sesión es obligatoria para emitir un JWT. */
    @Test
    void rejectsMissingSessionExpiry() {
        contextRunner.run(context -> {
            AccessTokenService service = context.getBean(AccessTokenService.class);

            // Sin una caducidad de sesión, el servicio rechaza la emisión del token.
            assertThatThrownBy(() -> service.issue(25L, null))
                    .isInstanceOf(IllegalArgumentException.class)
                    .hasMessage("Session expiry is required");
        });
    }

    /** Las fechas se truncan a segundos completos sin alargar la sesión original. */
    @Test
    void handlesFractionalSecondsWithoutExtendingSession() {
        contextRunner.run(context -> {
            // Reutiliza el firmador y las propiedades reales, con un reloj a las 08:00:00.250.
            Clock fractionalClock = Clock.fixed(NOW.plusMillis(250), ZoneOffset.UTC);
            AccessTokenService service = new AccessTokenService(
                    context.getBean(JwtEncoder.class), context.getBean(JwtProperties.class), fractionalClock);
            JwtDecoder decoder = context.getBean(JwtDecoder.class);
            Instant sessionExpiresAt = NOW.plusSeconds(180).plusMillis(900);

            Jwt issuedToken = service.issue(25L, sessionExpiresAt);
            Jwt verifiedToken = decoder.decode(issuedToken.getTokenValue());

            // La emisión 08:00:00.250 se representa como 08:00:00 en el JWT.
            assertThat(verifiedToken.getIssuedAt()).isEqualTo(NOW);
            // La caducidad 08:03:00.900 se trunca a 08:03:00, sin redondear hacia arriba.
            assertThat(verifiedToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(180));
            // La caducidad firmada nunca supera el límite original de la sesión.
            assertThat(verifiedToken.getExpiresAt()).isBeforeOrEqualTo(sessionExpiresAt);

            // A las 08:00:00.500 no queda una caducidad representable posterior al iat truncado.
            Instant sessionEndingInSameSecond = NOW.plusMillis(500);
            // Rechaza la emisión si truncar ambas fechas produciría exp igual a iat.
            assertThatThrownBy(() -> service.issue(25L, sessionEndingInSameSecond))
                    .isInstanceOf(IllegalArgumentException.class)
                    .hasMessage("Session expiry must allow a positive JWT lifetime");
        });
    }
}
