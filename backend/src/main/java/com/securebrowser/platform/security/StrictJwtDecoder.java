package com.securebrowser.platform.security;

import java.text.ParseException;
import java.util.List;
import java.util.Map;
import java.util.Objects;

import com.nimbusds.jwt.SignedJWT;

import org.springframework.security.oauth2.jwt.BadJwtException;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtDecoder;

/** Exige los tipos originales del JSON antes de las conversiones del decoder. */
public final class StrictJwtDecoder implements JwtDecoder {

    // Mensaje común que evita incluir el token o sus valores en el rechazo.
    private static final String INVALID_TOKEN_MESSAGE = "Invalid bearer token";

    // Decodificador que verificará la firma, el algoritmo y los valores de claims.
    private final JwtDecoder delegate;

    /** Recibe el decodificador de Spring que realizará la validación completa. */
    public StrictJwtDecoder(JwtDecoder delegate) {
        this.delegate = Objects.requireNonNull(delegate, "JWT decoder is required");
    }

    /** Comprueba los tipos del JSON y delega la verificación del mismo token. */
    @Override
    public Jwt decode(String token) {
        if (token == null || token.isBlank()) {
            throw new BadJwtException(INVALID_TOKEN_MESSAGE);
        }

        Map<String, Object> claims;
        try {
            // La lectura del payload conserva los tipos originales y no verifica la firma.
            claims = SignedJWT.parse(token).getPayload().toJSONObject();
        } catch (ParseException | IllegalArgumentException exception) {
            throw new BadJwtException(INVALID_TOKEN_MESSAGE);
        }

        if (claims == null
                || !(claims.get("sub") instanceof String)
                || !(claims.get("iss") instanceof String)
                || !(claims.get("jti") instanceof String)
                || !hasValidAudienceType(claims.get("aud"))
                || !isNumericDate(claims.get("iat"))
                || !isNumericDate(claims.get("exp"))
                || (claims.containsKey("nbf") && !isNumericDate(claims.get("nbf")))) {
            throw new BadJwtException(INVALID_TOKEN_MESSAGE);
        }

        // La aceptación siempre requiere que el decodificador verifique el JWT completo.
        return delegate.decode(token);
    }

    /** aud admite una cadena o una lista de cadenas, según el formato JWT. */
    private static boolean hasValidAudienceType(Object claim) {
        return claim instanceof String
                || (claim instanceof List<?> audiences
                        && audiences.stream().allMatch(String.class::isInstance));
    }

    /** NumericDate debe ser un número JSON finito, no una cadena numérica. */
    private static boolean isNumericDate(Object claim) {
        return claim instanceof Number number && Double.isFinite(number.doubleValue());
    }
}
