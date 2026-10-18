package com.securebrowser.platform.security;

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
import com.nimbusds.jose.crypto.MACSigner;

import org.springframework.boot.test.context.TestComponent;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.security.access.prepost.PreAuthorize;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.RestController;

/** Recursos exclusivos de pruebas: tokens firmados y un endpoint que observa la identidad. */
final class SecurityHttpTestSupport {

    // Instante fijo y clave conocida, disponibles únicamente en el classpath de pruebas.
    static final Instant NOW = Instant.parse("2026-10-18T08:00:00Z");
    static final byte[] TEST_KEY = new byte[32];
    static final String COMPANY_PATH = "/api/v1/companies/me";

    private SecurityHttpTestSupport() {
    }

    /** Propiedades del contexto HTTP aislado, sin configuración de producción. */
    static Map<String, Object> properties() {
        return Map.of(
                "sbp.security.jwt.secret-base64", Base64.getEncoder().encodeToString(TEST_KEY),
                "sbp.security.jwt.issuer", "sbp-test",
                "sbp.security.jwt.audience", "sbp-api-test");
    }

    /** Genera un JWT exclusivo de pruebas para un identificador persistido o simulado. */
    static String token(long userId) throws JOSEException {
        return token(userId, Map.of());
    }

    /** Permite alterar claims para comprobar que los filtros rechazan o ignoran esos datos. */
    static String token(long userId, Map<String, Object> overrides) throws JOSEException {
        Map<String, Object> claims = new LinkedHashMap<>();
        claims.put("sub", Long.toString(userId));
        claims.put("iss", "sbp-test");
        claims.put("aud", List.of("sbp-api-test"));
        claims.put("iat", NOW.minusSeconds(30).getEpochSecond());
        claims.put("exp", NOW.plusSeconds(30).getEpochSecond());
        claims.put("jti", "http-test-token");
        claims.putAll(overrides);

        JWSObject jwt = new JWSObject(new JWSHeader.Builder(JWSAlgorithm.HS256)
                .type(JOSEObjectType.JWT).build(), new Payload(claims));
        jwt.sign(new MACSigner(TEST_KEY));
        return jwt.serialize();
    }

    /** Importa explícitamente el reloj y los recursos que nunca forman parte del JAR. */
    @TestConfiguration(proxyBeanMethods = false)
    @Import({ProbeController.class, MethodProtectedProbe.class, ClockConfiguration.class})
    static class ProbeConfiguration {
    }

    /** Permite reutilizar el reloj sin registrar el endpoint de prueba en el contexto real. */
    @TestConfiguration(proxyBeanMethods = false)
    static class ClockConfiguration {
        /** Hace reproducibles las fechas de todos los tokens de las pruebas HTTP. */
        @Bean
        @Primary
        Clock securityHttpTestClock() {
            return Clock.fixed(NOW, ZoneOffset.UTC);
        }
    }

    /** Observa el principal sin implementar consultas o modificaciones reales de Company. */
    @TestComponent
    @RestController
    static class ProbeController {

        private final MethodProtectedProbe methodProbe;

        ProbeController(MethodProtectedProbe methodProbe) {
            this.methodProbe = methodProbe;
        }

        @GetMapping(COMPANY_PATH)
        public Map<String, Object> get(@AuthenticationPrincipal AuthenticatedUser principal) {
            return Map.of("userId", principal.userId(), "companyId", principal.companyId(),
                    "role", principal.role().name());
        }

        @PatchMapping(COMPANY_PATH)
        public Map<String, Object> patch(@AuthenticationPrincipal AuthenticatedUser principal) {
            methodProbe.adminOperation();
            return get(principal);
        }
    }

    /** Permite comprobar la autorización de métodos sobre un bean gestionado por Spring. */
    @TestComponent
    static class MethodProtectedProbe {

        @PreAuthorize("hasRole('ADMIN')")
        public void adminOperation() {
            // No escribe datos: la prueba comprueba el interceptor de permisos.
        }
    }
}
