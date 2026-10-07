package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Objects;
import java.util.Optional;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las relaciones, restricciones y auditoría de UrlRule en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class UrlRulePersistenceTest {

    // Permite guardar entidades y consultar las filas de PostgreSQL.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite utilizar fechas conocidas sin esperas entre modificaciones.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Guarda las reglas junto con su política y reconstruye ambas relaciones. */
    @Test
    void persistsRulesWithPolicyByCascade() {
        Policy policy = persistPolicy("  EXAMPLE.COM.  ", "bücher.de");
        Long policyId = policy.getId();
        Long companyId = policy.getCompany().getId();
        entityManager.clear();

        Policy stored = entityManager.find(Policy.class, policyId);

        assertThat(stored.getUrlRules()).extracting(element -> Objects.requireNonNull(element).getDomain())
                .containsExactlyInAnyOrder("example.com", "xn--bcher-kva.de");
        assertThat(stored.getUrlRules()).allSatisfy(rule -> {
            assertThat(rule.getId()).isPositive();
            assertThat(rule.getPolicy().getId()).isEqualTo(policyId);
            assertThat(rule.getPolicy().getCompany().getId()).isEqualTo(companyId);
        });
    }

    /** La auditoría actualiza la política al añadir, editar y retirar reglas. */
    @Test
    void ruleChangesUpdatePolicyAuditDatesAndPreserveCreation() {
        Instant creation = Instant.parse("2026-10-06T08:00:00Z");
        Instant addition = creation.plusSeconds(60);
        Instant edition = creation.plusSeconds(120);
        Instant removal = creation.plusSeconds(180);

        try {
            auditingHandler.setDateTimeProvider(() -> Optional.of(creation));
            Policy policy = persistPolicy();
            Long policyId = policy.getId();
            entityManager.clear();

            Policy stored = entityManager.find(Policy.class, policyId);
            assertThat(stored.getUrlRules()).isEmpty();
            assertThat(stored.getCreatedAt()).isEqualTo(creation);
            assertThat(stored.getUpdatedAt()).isEqualTo(creation);

            auditingHandler.setDateTimeProvider(() -> Optional.of(addition));
            UrlRule rule = stored.addUrlRule("EXAMPLE.COM.");
            entityManager.flush();
            Long ruleId = rule.getId();
            entityManager.clear();

            Policy afterAddition = entityManager.find(Policy.class, policyId);
            assertThat(afterAddition.getCreatedAt()).isEqualTo(creation);
            assertThat(afterAddition.getUpdatedAt()).isEqualTo(addition);
            assertThat(afterAddition.getUrlRules()).extracting(element -> Objects.requireNonNull(element).getDomain())
                    .containsExactly("example.com");
            entityManager.clear();

            // La edición también funciona al cargar la regla y su política LAZY.
            auditingHandler.setDateTimeProvider(() -> Optional.of(edition));
            UrlRule loadedRule = entityManager.find(UrlRule.class, ruleId);
            loadedRule.changeDomain("BÜCHER.DE.");
            entityManager.flush();
            entityManager.clear();

            Policy afterEdition = entityManager.find(Policy.class, policyId);
            assertThat(afterEdition.getCreatedAt()).isEqualTo(creation);
            assertThat(afterEdition.getUpdatedAt()).isEqualTo(edition);
            assertThat(afterEdition.getUrlRules()).extracting(element -> Objects.requireNonNull(element).getDomain())
                    .containsExactly("xn--bcher-kva.de");

            auditingHandler.setDateTimeProvider(() -> Optional.of(removal));
            afterEdition.removeUrlRule(afterEdition.getUrlRules().getFirst());
            entityManager.flush();
            entityManager.clear();

            Policy afterRemoval = entityManager.find(Policy.class, policyId);
            assertThat(afterRemoval.getCreatedAt()).isEqualTo(creation);
            assertThat(afterRemoval.getUpdatedAt()).isEqualTo(removal);
            assertThat(afterRemoval.getUrlRules()).isEmpty();
            assertThat(entityManager.find(UrlRule.class, ruleId)).isNull();
        } finally {
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** PostgreSQL impide duplicados incluso cuando se evita la validación Java. */
    @Test
    void databaseRejectsDuplicateDomainWithinPolicy() {
        Policy policy = persistPolicy("example.com");

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "INSERT INTO url_rules (policy_id, domain) VALUES (:policyId, :domain)")
                .setParameter("policyId", policy.getId())
                .setParameter("domain", "example.com")
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_url_rules_policy_domain");
    }

    /** La unicidad permite reutilizar un dominio en otra política. */
    @Test
    void databaseAllowsSameDomainInDifferentPolicies() {
        Policy first = persistPolicy("example.com");
        Policy second = persistPolicy("EXAMPLE.COM.");
        Long firstId = first.getId();
        Long secondId = second.getId();
        entityManager.clear();

        UrlRule firstRule = entityManager.find(Policy.class, firstId).getUrlRules().getFirst();
        UrlRule secondRule = entityManager.find(Policy.class, secondId).getUrlRules().getFirst();

        assertThat(firstRule.getId()).isPositive().isNotEqualTo(secondRule.getId());
        assertThat(firstRule.getDomain()).isEqualTo(secondRule.getDomain());
        assertThat(firstRule.getPolicy().getId()).isNotEqualTo(secondRule.getPolicy().getId());
    }

    /** El CHECK admite únicamente dominios ASCII con el formato ya normalizado. */
    @ParameterizedTest
    @ValueSource(strings = {
            "", "   ", "EXAMPLE.COM", "example.com.", "bücher.de",
            "https://example.com", "example.com/path", "example.com:443",
            "*.example.com", "example..com", "-example.com", "exam_ple.com"
    })
    void databaseRejectsInvalidOrUnnormalizedDomain(String domain) {
        Long ruleId = persistPolicy("example.com").getUrlRules().getFirst().getId();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE url_rules SET domain = :domain WHERE id = :id")
                .setParameter("domain", domain)
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_url_rules_domain");
    }

    /** Comprueba que PostgreSQL exige la política y el dominio. */
    @ParameterizedTest
    @ValueSource(strings = {"policy_id", "domain"})
    void databaseRejectsMissingRequiredValue(String column) {
        Long ruleId = persistPolicy("example.com").getUrlRules().getFirst().getId();

        // La columna procede únicamente de los dos casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE url_rules SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** Comprueba que la clave foránea rechaza una política inexistente. */
    @Test
    void databaseRejectsUnknownPolicy() {
        Long ruleId = persistPolicy("example.com").getUrlRules().getFirst().getId();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE url_rules SET policy_id = :policyId WHERE id = :id")
                .setParameter("policyId", Long.MAX_VALUE)
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_url_rules_policy");
    }

    /** La eliminación de la política mediante JPA elimina sus reglas. */
    @Test
    void deletingPolicyByJpaDeletesRulesAndKeepsCompany() {
        Policy policy = persistPolicy("example.com");
        Long policyId = policy.getId();
        Long ruleId = policy.getUrlRules().getFirst().getId();
        Long companyId = policy.getCompany().getId();

        entityManager.remove(policy);
        entityManager.flush();
        entityManager.clear();

        assertThat(entityManager.find(Policy.class, policyId)).isNull();
        assertThat(entityManager.find(UrlRule.class, ruleId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** La clave foránea también elimina las reglas al borrar con SQL directo. */
    @Test
    void databaseCascadesPolicyDeletionAndKeepsCompany() {
        Policy policy = persistPolicy("example.com");
        Long policyId = policy.getId();
        Long ruleId = policy.getUrlRules().getFirst().getId();
        Long companyId = policy.getCompany().getId();

        entityManager.createNativeQuery("DELETE FROM policies WHERE id = :id")
                .setParameter("id", policyId)
                .executeUpdate();
        entityManager.clear();

        assertThat(entityManager.find(Policy.class, policyId)).isNull();
        assertThat(entityManager.find(UrlRule.class, ruleId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** Guarda una política con sus dominios y una compañía propia para la prueba. */
    private Policy persistPolicy(String... domains) {
        Company company = new Company("Empresa A");
        entityManager.persist(company);
        Policy policy = new Policy("Política A", company);
        for (String domain : domains) {
            policy.addUrlRule(domain);
        }
        entityManager.persist(policy);
        entityManager.flush();
        return policy;
    }
}
