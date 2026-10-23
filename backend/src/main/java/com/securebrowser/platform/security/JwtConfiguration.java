package com.securebrowser.platform.security;

import java.time.Clock;
import java.util.Map;

import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.boot.context.properties.EnableConfigurationProperties;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.security.oauth2.jose.jws.MacAlgorithm;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.jwt.JwtEncoder;
import org.springframework.security.oauth2.jwt.MappedJwtClaimSetConverter;
import org.springframework.security.oauth2.jwt.NimbusJwtDecoder;
import org.springframework.security.oauth2.jwt.NimbusJwtEncoder;

/** Conecta la configuración externa con la firma y verificación de JWT. */
@Configuration(proxyBeanMethods = false)
@EnableConfigurationProperties(JwtProperties.class)
public class JwtConfiguration {

    /** Proporciona el reloj UTC, permitiendo sustituirlo por uno fijo en pruebas. */
    @Bean
    @ConditionalOnMissingBean(Clock.class)
    public Clock jwtClock() {
        return Clock.systemUTC();
    }

    /** Prepara la firma HS256 con la misma clave que utiliza el verificador. */
    @Bean
    public JwtEncoder jwtEncoder(JwtProperties properties) {
        return NimbusJwtEncoder.withSecretKey(properties.getSecretKey())
                .algorithm(MacAlgorithm.HS256)
                .build();
    }

    /** Verifica HS256, conserva los tipos originales y aplica nuestras reglas. */
    @Bean
    public JwtDecoder jwtDecoder(JwtProperties properties, Clock clock) {
        NimbusJwtDecoder delegate = NimbusJwtDecoder
                .withSecretKey(properties.getSecretKey())
                .macAlgorithm(MacAlgorithm.HS256)
                .build();

        // Conserva exactamente el emisor recibido, sin normalizarlo como una URL.
        delegate.setClaimSetConverter(MappedJwtClaimSetConverter
                .withDefaults(Map.of("iss", value -> value)));
        delegate.setJwtValidator(new JwtClaimsValidator(properties, clock));

        return new StrictJwtDecoder(delegate);
    }
}
