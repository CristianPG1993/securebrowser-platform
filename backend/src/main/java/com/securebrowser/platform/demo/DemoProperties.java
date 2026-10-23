package com.securebrowser.platform.demo;

import java.nio.charset.StandardCharsets;
import org.springframework.boot.context.properties.ConfigurationProperties;

/** Las contraseñas iniciales del demo se reciben del entorno, nunca del repositorio. */
@ConfigurationProperties("sbp.demo")
public record DemoProperties(String adminPassword, String userPassword) {
    public DemoProperties {
        validate(adminPassword);
        validate(userPassword);
    }

    private static void validate(String value) {
        if (value == null || value.codePointCount(0, value.length()) < 12
                || value.getBytes(StandardCharsets.UTF_8).length > 72) {
            throw new IllegalArgumentException("Demo passwords require at least 12 characters and at most 72 UTF-8 bytes");
        }
    }

    @Override
    public String toString() {
        return "DemoProperties[credentials=redacted]";
    }
}
