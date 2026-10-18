package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Stream;

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
import org.junit.jupiter.params.provider.Arguments;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.security.oauth2.jwt.BadJwtException;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtDecoder;

/** Comprueba los tipos originales del JSON y la delegación de la validación. */
class StrictJwtDecoderTest {

    // Sustituye la verificación completa para aislar las comprobaciones de formato.
    private final JwtDecoder delegate = mock(JwtDecoder.class);

    private final StrictJwtDecoder decoder = new StrictJwtDecoder(delegate);

    /** aud puede ser una cadena o una lista de cadenas en el JSON original. */
    @ParameterizedTest
    @ValueSource(booleans = {false, true})
    void delegatesOriginalTokenWithValidClaimTypes(boolean audienceAsList)
            throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put("aud", audienceAsList ? List.of("sbp-api-test") : "sbp-api-test");
        String token = signed(claims);
        Jwt expected = mock(Jwt.class);
        when(delegate.decode(token)).thenReturn(expected);

        assertThat(decoder.decode(token)).isSameAs(expected);
        verify(delegate).decode(token);
        verifyNoMoreInteractions(delegate);
    }

    /** Rechaza entradas vacías o malformadas antes de llamar al delegado. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {" ", "not-a-jwt", "a.b.c"})
    void rejectsMalformedToken(String token) {
        assertRejectedBeforeDelegate(token);
    }

    /** Ningún claim obligatorio puede completarse mediante conversiones posteriores. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "iss", "aud", "iat", "exp", "jti"})
    void rejectsMissingRequiredClaim(String claimName) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.remove(claimName);

        assertRejectedBeforeDelegate(signed(claims));
    }

    /** Un claim obligatorio con null tampoco es una entrada válida. */
    @ParameterizedTest
    @ValueSource(strings = {"sub", "iss", "aud", "iat", "exp", "jti"})
    void rejectsNullRequiredClaim(String claimName) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put(claimName, null);

        assertRejectedBeforeDelegate(signed(claims));
    }

    /** Conserva la distinción entre texto, números, listas y otros tipos JSON. */
    @ParameterizedTest
    @MethodSource("invalidRawClaims")
    void rejectsWrongOriginalClaimType(String claimName, Object value)
            throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put(claimName, value);

        assertRejectedBeforeDelegate(signed(claims));
    }

    /** Rechaza un payload firmado cuyo contenido no es un objeto JSON válido. */
    @Test
    void rejectsMalformedJsonPayload() throws Exception {
        assertRejectedBeforeDelegate(signPayload(new Payload("{")));
    }

    /** Un JWT sin firma no se entrega al delegado como token firmado válido. */
    @Test
    void rejectsUnsignedToken() {
        String token = new PlainObject(new Payload(validRawClaims())).serialize();

        assertRejectedBeforeDelegate(token);
    }

    /** El formato correcto no sustituye la aceptación del decodificador completo. */
    @Test
    void propagatesDelegateRejection() throws Exception {
        String token = signed(validRawClaims());
        BadJwtException failure = new BadJwtException("Signature validation failed");
        when(delegate.decode(token)).thenThrow(failure);

        assertThatThrownBy(() -> decoder.decode(token)).isSameAs(failure);
        verify(delegate).decode(token);
        verifyNoMoreInteractions(delegate);
    }

    /** nbf también permite un NumericDate entero o con parte decimal. */
    @ParameterizedTest
    @ValueSource(doubles = {1792303200.0, 1792303200.5})
    void delegatesNumericNotBefore(double notBefore) throws Exception {
        Map<String, Object> claims = validRawClaims();
        claims.put("nbf", notBefore);
        String token = signed(claims);
        Jwt expected = mock(Jwt.class);
        when(delegate.decode(token)).thenReturn(expected);

        assertThat(decoder.decode(token)).isSameAs(expected);
        verify(delegate).decode(token);
        verifyNoMoreInteractions(delegate);
    }

    /** Comprueba que el rechazo es genérico y ocurre antes de la delegación. */
    private void assertRejectedBeforeDelegate(String token) {
        assertThatThrownBy(() -> decoder.decode(token))
                .isInstanceOf(BadJwtException.class)
                .hasMessage("Invalid bearer token");
        verifyNoInteractions(delegate);
    }

    /** Utiliza los tipos originales del JSON, antes de convertir fechas a Instant. */
    private static Map<String, Object> validRawClaims() {
        Map<String, Object> claims = new LinkedHashMap<>();
        claims.put("sub", "25");
        claims.put("iss", "sbp-test");
        claims.put("aud", List.of("sbp-api-test"));
        claims.put("iat", 1792303200L);
        claims.put("exp", 1792304100L);
        claims.put("jti", "test-token-id");
        return claims;
    }

    /** Serializa el JSON original sin convertir ni normalizar sus claims. */
    private static String signed(Map<String, Object> claims) throws JOSEException {
        return signPayload(new Payload(claims));
    }

    /** Firma únicamente tokens de prueba con una clave conocida y aislada. */
    private static String signPayload(Payload payload) throws JOSEException {
        JWSHeader header = new JWSHeader.Builder(JWSAlgorithm.HS256)
                .type(JOSEObjectType.JWT)
                .build();
        JWSObject token = new JWSObject(header, payload);
        token.sign(new MACSigner(new byte[32]));
        return token.serialize();
    }

    /** Casos que una conversión automática no debe transformar en datos válidos. */
    private static Stream<Arguments> invalidRawClaims() {
        return Stream.of(
                Arguments.of("sub", 25L),
                Arguments.of("sub", true),
                Arguments.of("sub", List.of("25")),
                Arguments.of("iss", 7L),
                Arguments.of("iss", false),
                Arguments.of("jti", 7L),
                Arguments.of("jti", List.of("test-token-id")),
                Arguments.of("aud", 7L),
                Arguments.of("aud", List.of("sbp-api-test", 7)),
                Arguments.of("aud", List.of("sbp-api-test", false)),
                Arguments.of("iat", "1792303200"),
                Arguments.of("iat", true),
                Arguments.of("iat", List.of(1792303200L)),
                Arguments.of("exp", "1792304100"),
                Arguments.of("exp", false),
                Arguments.of("exp", List.of(1792304100L)),
                Arguments.of("nbf", "1792303200"),
                Arguments.of("nbf", true),
                Arguments.of("nbf", (Object) null));
    }
}
