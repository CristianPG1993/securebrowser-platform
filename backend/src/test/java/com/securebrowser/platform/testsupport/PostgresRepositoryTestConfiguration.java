package com.securebrowser.platform.testsupport;

import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.boot.testcontainers.service.connection.ServiceConnection;
import org.springframework.context.annotation.Bean;
import org.testcontainers.postgresql.PostgreSQLContainer;

/** Comparte un PostgreSQL temporal entre los tests de consultas y bloqueos de repositorios. */
@TestConfiguration(proxyBeanMethods = false)
public class PostgresRepositoryTestConfiguration {

    /** Spring proporciona la conexión y gestiona el ciclo de vida del contenedor. */
    @Bean
    @ServiceConnection
    PostgreSQLContainer postgresContainer() {
        return new PostgreSQLContainer("postgres:17")
                .withDatabaseName("securebrowser_db");
    }
}
