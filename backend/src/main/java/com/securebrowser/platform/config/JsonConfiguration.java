package com.securebrowser.platform.config;

import tools.jackson.databind.DeserializationFeature;
import tools.jackson.databind.cfg.CoercionAction;
import tools.jackson.databind.cfg.CoercionInputShape;
import tools.jackson.databind.type.LogicalType;

import org.springframework.boot.jackson.autoconfigure.JsonMapperBuilderCustomizer;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/** Aplica las reglas de lectura estricta del JSON recibido por la API. */
@Configuration(proxyBeanMethods = false)
public class JsonConfiguration {

    /** Personaliza el mapper de Spring Boot sin sustituir su configuración completa. */
    @Bean
    public JsonMapperBuilderCustomizer strictJsonInput() {
        return builder -> {
            // Rechaza campos ajenos al DTO y contenido adicional tras el JSON principal.
            builder.enable(
                    DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES,
                    DeserializationFeature.FAIL_ON_TRAILING_TOKENS);

            // Un campo de texto debe recibir texto, no un número o booleano convertido.
            builder.withCoercionConfig(LogicalType.Textual, coercion -> coercion
                    .setCoercion(CoercionInputShape.Integer, CoercionAction.Fail)
                    .setCoercion(CoercionInputShape.Float, CoercionAction.Fail)
                    .setCoercion(CoercionInputShape.Boolean, CoercionAction.Fail));
        };
    }
}
