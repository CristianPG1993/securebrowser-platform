package com.securebrowser.platform.config;

import org.springframework.context.annotation.Configuration;
import org.springframework.data.jpa.repository.config.EnableJpaAuditing;

/**
 * Activa la auditoría de creación y modificación de las entidades JPA.
 */
@Configuration
@EnableJpaAuditing(modifyOnCreate = true)
public class JpaAuditingConfig {
}