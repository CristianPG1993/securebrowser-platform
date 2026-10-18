package com.securebrowser.platform.company;

import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.testcontainers.service.connection.ServiceConnection;
import org.springframework.context.annotation.Bean;
import org.testcontainers.postgresql.PostgreSQLContainer;

/** Prepara un PostgreSQL temporal exclusivo para las pruebas de integración de Company. */
@TestConfiguration(proxyBeanMethods = false)
class CompanyApiTestConfiguration {

    /** Spring gestiona el contenedor y proporciona su conexión a JPA y Flyway. */
    @Bean
    @ServiceConnection
    PostgreSQLContainer postgresContainer() {
        return new PostgreSQLContainer("postgres:17")
                .withDatabaseName("securebrowser_db");
    }
}
