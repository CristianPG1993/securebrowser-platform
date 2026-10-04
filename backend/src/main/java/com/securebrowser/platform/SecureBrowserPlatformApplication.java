package com.securebrowser.platform;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

/**
 * Punto de entrada del backend de SecureBrowser Platform.
 */
@SpringBootApplication
public class SecureBrowserPlatformApplication {

    /**
     * Inicia el contexto de Spring y el servidor web.
     */
    public static void main(String[] args) {
        SpringApplication.run(SecureBrowserPlatformApplication.class, args);
    }
}