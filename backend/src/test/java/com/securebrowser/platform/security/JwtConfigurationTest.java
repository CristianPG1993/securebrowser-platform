package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.Base64;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import com.nimbusds.jose.JOSEException;
import com.nimbusds.jose.JOSEObjectType;
import com.nimbusds.jose.JWSAlgorithm;
import com.nimbusds.jose.JWSHeader;
import com.nimbusds.jose.JWSObject;
import com.nimbusds.jose.Payload;
import com.nimbusds.jose.PlainObject;
import com.nimbusds.jose.crypto.MACSigner;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.runner.ApplicationContextRunner;
import org.springframework.security.oauth2.jose.jws.MacAlgorithm;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtClaimsSet;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.JwtEncoderParameters;
import org.springframework.security.oauth2.jwt.JwtException;
import org.springframework.security.oauth2.jwt.JwsHeader;

/** Comprueba la configuración completa con firmas reales y sin base de datos. */
class JwtConfigurationTest {

    // Fecha fija para que los resultados no dependan del momento de ejecución.
    private static final Instant NOW = Instant.parse("2026-10-18T08:00:00Z");

    // Clave conocida de prueba de 64 bytes: permite firmar también HS384 y HS512.
    private static final byte[] TEST_KEY = new byte[64];

    // Carga únicamente nuestra configuración JWT, sin arrancar todo el backend.
    private final ApplicationContextRunner contextRunner = new ApplicationContextRunner()
            .withUserConfiguration(JwtConfiguration.class)
            .withPropertyValues(
                    "sbp.security.jwt.secret-base64="
                            + Base64.getEncoder().encodeToString(TEST_KEY),
                    "sbp.security.jwt.issuer=sbp-test",
                    "sbp.security.jwt.audience=sbp-api-test");

    // Sustituye el reloj del sistema por el instante conocido de estas pruebas.
    private final ApplicationContextRunner fixedClockRunner = contextRunner
            .withBean(Clock.class, () -> Clock.fixed(NOW, ZoneOffset.UTC));

    /** Registra un reloj UTC y el decodificador cuando no se proporciona un reloj. */
    @Test
    void registersDefaultClockAndDecoder() {
        contextRunner.run(context -> {
            assertThat(context).hasNotFailed();
            assertThat(context).hasSingleBean(Clock.class);
            assertThat(context).hasSingleBean(JwtDecoder.class);
            assertThat(context.getBean(Clock.class).getZone()).isEqualTo(ZoneOffset.UTC);
            assertThat(context.getBean(JwtDecoder.class))
                    .isInstanceOf(StrictJwtDecoder.class);
        });
    }

    /** El firmador real produce un JWT que acepta nuestro verificador real. */
    @Test
    void encoderProducesTokenAcceptedByDecoder() {
        fixedClockRunner.run(context -> {
            // La configuración con la clave de prueba debe arrancar correctamente.
            assertThat(context).hasNotFailed();

            // Spring debe proporcionar un único firmador JWT.
            assertThat(context).hasSingleBean(JwtEncoder.class);

            JwtEncoder encoder = context.getBean(JwtEncoder.class);
            JwtDecoder decoder = context.getBean(JwtDecoder.class);

            JwsHeader header = JwsHeader.with(MacAlgorithm.HS256).type("JWT").build();
            JwtClaimsSet claims = JwtClaimsSet.builder()
                    .subject("25")
                    .issuer("sbp-test")
                    .audience(List.of("sbp-api-test"))
                    .issuedAt(NOW)
                    .expiresAt(NOW.plusSeconds(900))
                    .id("encoder-test-token")
                    .build();

            Jwt signed = encoder.encode(JwtEncoderParameters.from(header, claims));
            Jwt verified = decoder.decode(signed.getTokenValue());

            // El token recibido utiliza el algoritmo HS256 acordado.
            assertThat(verified.getHeaders().get("alg")).isEqualTo("HS256");

            // El usuario identificado sigue siendo el 25 tras firmar y verificar.
            assertThat(verified.getSubject()).isEqualTo("25");

            // El emisor conserva exactamente el texto configurado.
            assertThat(verified.getClaims().get("iss")).isEqualTo("sbp-test");

            // El token está dirigido a nuestra API de prueba.
            assertThat(verified.getAudience()).containsExactly("sbp-api-test");

            // La emisión coincide con el instante del reloj fijo.
            assertThat(verified.getIssuedAt()).isEqualTo(NOW);

            // La caducidad se conserva a los 900 segundos de la emisión.
            assertThat(verified.getExpiresAt()).isEqualTo(NOW.plusSeconds(900));

            // El identificador del token tampoco cambia en el recorrido.
            assertThat(verified.getId()).isEqualTo("encoder-test-token");
        });
    }

    /** Valida una firma real y convierte los claims utilizando el reloj inyectado. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void acceptsSignedTokenWithExpectedClaims(boolean audienceAsList) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put("aud", audienceAsList ? List.of("sbp-api-test") : "sbp-api-test");
        String token = signed(claims);

        fixedClockRunner.run(context -> {
            assertThat(context).hasNotFailed();
            assertThat(context).hasSingleBean(Clock.class);
            assertThat(context.getBean(Clock.class).instant()).isEqualTo(NOW);

            Jwt jwt = context.getBean(JwtDecoder.class).decode(token);

            assertThat(jwt.getSubject()).isEqualTo("25");
            assertThat(jwt.getClaims().get("iss")).isEqualTo("sbp-test");
            assertThat(jwt.getAudience()).containsExactly("sbp-api-test");
            assertThat(jwt.getIssuedAt()).isEqualTo(NOW.minusSeconds(30));
            assertThat(jwt.getExpiresAt()).isEqualTo(NOW.plusSeconds(30));
            assertThat(jwt.getId()).isEqualTo("test-token-id");
        });
    }

    /** La firma no es válida cuando se utiliza una clave diferente. */
    @Test
    void rejectsTokenSignedWithAnotherKey() throws Exception {
        byte[] anotherKey = TEST_KEY.clone();
        anotherKey[0] = 1;

        assertRejected(signed(validRawClaims(), JWSAlgorithm.HS256, anotherKey));
    }

    /** Alterar el JSON sin volver a firmar invalida el token. */
    @Test
    void rejectsTamperedPayload() throws Exception {
        String[] parts = signed(validRawClaims()).split("\\.");
        Map<String, Object> alteredClaims = validRawClaims();
        alteredClaims.put("sub", "99");
        String alteredPayload = new Payload(alteredClaims).toBase64URL().toString();

        assertRejected(parts[0] + "." + alteredPayload + "." + parts[2]);
    }

    /** Rechaza otros algoritmos aunque la firma sea correcta para la misma clave. */
    @ParameterizedTest
    @ValueSource(strings = {"HS384", "HS512"})
    void rejectsAlgorithmsOtherThanHs256(String algorithm) throws Exception {
        assertRejected(signed(validRawClaims(), JWSAlgorithm.parse(algorithm), TEST_KEY));
    }

    /** Un token sin firma tampoco puede autenticarse. */
    @Test
    void rejectsUnsignedToken() {
        assertRejected(new PlainObject(new Payload(validRawClaims())).serialize());
    }

    /** Exige los seis claims originales, incluido iat antes de cualquier conversión. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "iss", "aud", "iat", "exp", "jti"})
    void rejectsMissingRequiredClaim(String claimName) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.remove(claimName);

        assertRejected(signed(claims));
    }

    /** Impide aceptar tipos incorrectos mediante conversiones automáticas. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "aud", "iat", "exp"})
    void rejectsWrongOriginalClaimTypes(String claimName) throws Exception {
        Map<String, Object> claims = validRawClaims();
        switch (claimName) {
            case "sub" -> claims.put("sub", 25L);
            case "aud" -> claims.put("aud", List.of("sbp-api-test", 7));
            case "iat" -> claims.put("iat", Long.toString(NOW.minusSeconds(30).getEpochSecond()));
            case "exp" -> claims.put("exp", Long.toString(NOW.plusSeconds(30).getEpochSecond()));
            default -> throw new IllegalArgumentException("Unknown test claim");
        }

        assertRejected(signed(claims));
    }

    /** Una firma válida no permite ignorar el emisor, la audiencia ni la identidad. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "iss", "aud", "jti"})
    void rejectsUnexpectedClaimValues(String claimName) throws Exception {
        Map<String, Object> claims = validRawClaims();
        switch (claimName) {
            case "sub" -> claims.put("sub", "0");
            case "iss" -> claims.put("iss", "another-issuer");
            case "aud" -> claims.put("aud", "another-api");
            case "jti" -> claims.put("jti", " ");
            default -> throw new IllegalArgumentException("Unknown test claim");
        }

        assertRejected(signed(claims));
    }

    /** El token solo es válido antes de exp, sin margen adicional de caducidad. */
    @ParameterizedTest
    @ValueSource(longs = {-1L, 0L, 1L})
    void checksExpirationAgainstInjectedClock(long secondsUntilExpiration) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put("exp", NOW.plusSeconds(secondsUntilExpiration).getEpochSecond());
        String token = signed(claims);

        if (secondsUntilExpiration <= 0) {
            assertRejected(token);
        } else {
            fixedClockRunner.run(context ->
                    assertThat(context.getBean(JwtDecoder.class).decode(token)).isNotNull());
        }
    }

    /** Rechaza emisiones futuras, periodos invertidos y duraciones excesivas. */
    @ParameterizedTest
    @ValueSource(strings = {"future-iat", "exp-before-iat", "excessive-lifetime"})
    void rejectsInvalidTimeWindow(String scenario) throws Exception {
        Map<String, Object> claims = validRawClaims();
        switch (scenario) {
            case "future-iat" -> claims.put("iat", NOW.plusSeconds(1).getEpochSecond());
            case "exp-before-iat" -> claims.put("exp", NOW.minusSeconds(31).getEpochSecond());
            case "excessive-lifetime" -> claims.put("exp", NOW.plusSeconds(871).getEpochSecond());
            default -> throw new IllegalArgumentException("Unknown test scenario");
        }

        assertRejected(signed(claims));
    }

    /** Aplica también la fecha opcional a partir de la cual puede usarse el token. */
    @ParameterizedTest
    @ValueSource(longs = {0L, 1L})
    void checksOptionalNotBefore(long secondsUntilNotBefore) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put("nbf", NOW.plusSeconds(secondsUntilNotBefore).getEpochSecond());
        String token = signed(claims);

        if (secondsUntilNotBefore > 0) {
            assertRejected(token);
        } else {
            fixedClockRunner.run(context ->
                    assertThat(context.getBean(JwtDecoder.class).decode(token)).isNotNull());
        }
    }

    /** Comprueba el rechazo usando el bean real creado por JwtConfiguration. */
    private void assertRejected(String token) {
        fixedClockRunner.run(context -> {
            assertThat(context).hasNotFailed();
            JwtDecoder decoder = context.getBean(JwtDecoder.class);

            assertThatThrownBy(() -> decoder.decode(token)).isInstanceOf(JwtException.class);
        });
    }

    /** Mantiene los tipos JSON originales para probar también StrictJwtDecoder. */
    private static Map<String, Object> validRawClaims() {
        Map<String, Object> claims = new LinkedHashMap<>();
        claims.put("sub", "25");
        claims.put("iss", "sbp-test");
        claims.put("aud", List.of("sbp-api-test"));
        claims.put("iat", NOW.minusSeconds(30).getEpochSecond());
        claims.put("exp", NOW.plusSeconds(30).getEpochSecond());
        claims.put("jti", "test-token-id");
        return claims;
    }

    /** Firma los casos habituales con HS256 y la clave exclusiva de pruebas. */
    private static String signed(Map<String, Object> claims) throws JOSEException {
        return signed(claims, JWSAlgorithm.HS256, TEST_KEY);
    }

    /** Permite variar la clave o el algoritmo para comprobar rechazos reales. */
    private static String signed(Map<String, Object> claims, JWSAlgorithm algorithm,
            byte[] key) throws JOSEException {
        JWSHeader header = new JWSHeader.Builder(algorithm).type(JOSEObjectType.JWT).build();
        JWSObject token = new JWSObject(header, new Payload(claims));
        token.sign(new MACSigner(key));
        return token.serialize();
    }
}
