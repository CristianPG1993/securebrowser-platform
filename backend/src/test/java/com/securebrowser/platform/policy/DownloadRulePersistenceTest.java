package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;
import java.util.Optional;

import com.securebrowser.platform.company.Company;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;

import org.hibernate.exception.ConstraintViolationException;
import org.hibernate.exception.DataException;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.data.auditing.AuditingHandler;
import org.springframework.data.auditing.CurrentDateTimeProvider;
import org.springframework.transaction.annotation.Transactional;

/** Comprueba las restricciones, relaciones y auditoría de descargas en PostgreSQL. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE)
@Transactional
class DownloadRulePersistenceTest {

    // Permite guardar entidades y comprobar las restricciones mediante SQL.
    @PersistenceContext
    private EntityManager entityManager;

    // Permite utilizar fechas conocidas sin esperas entre modificaciones.
    @Autowired
    private AuditingHandler auditingHandler;

    /** Guarda reglas de descarga y de URL mediante su política y las recupera. */
    @Test
    void persistsBothRuleCollectionsByCascade() {
        Policy policy = createPolicy();
        policy.addUrlRule("example.com");
        policy.addDownloadRule(".EXE");
        policy.addDownloadRule("PDF");
        policy.configureDownloadControl(false, FilterMode.ALLOWLIST);
        entityManager.persist(policy);
        entityManager.flush();
        Long policyId = policy.getId();
        Long companyId = policy.getCompany().getId();
        entityManager.clear();

        Policy stored = entityManager.find(Policy.class, policyId);

        assertThat(stored.getDownloadRules()).extracting(DownloadRule::getExtension)
                .containsExactlyInAnyOrder("exe", "pdf");
        assertThat(stored.getDownloadRules()).allSatisfy(rule -> {
            assertThat(rule.getId()).isPositive();
            assertThat(rule.getPolicy().getId()).isEqualTo(policyId);
            assertThat(rule.getPolicy().getCompany().getId()).isEqualTo(companyId);
            assertThat(rule.getPolicy().getDownloadControlMode()).isEqualTo(FilterMode.ALLOWLIST);
        });
        assertThat(stored.getUrlRules()).extracting(UrlRule::getDomain)
                .containsExactly("example.com");
        assertThat(stored.isDownloadControlEnabled()).isFalse();
    }

    /** Añadir, editar y retirar descargas actualiza solo la fecha de modificación. */
    @Test
    void ruleChangesUpdatePolicyAuditDatesAndPreserveCreationAndUrlRules() {
        Instant creation = Instant.parse("2026-10-07T08:00:00Z");
        Instant addition = creation.plusSeconds(60);
        Instant edition = creation.plusSeconds(120);
        Instant removal = creation.plusSeconds(180);

        try {
            auditingHandler.setDateTimeProvider(() -> Optional.of(creation));
            Policy policy = createPolicy();
            policy.addUrlRule("example.com");
            entityManager.persist(policy);
            entityManager.flush();
            Long policyId = policy.getId();
            entityManager.clear();

            Policy stored = entityManager.find(Policy.class, policyId);
            assertThat(stored.getDownloadRules()).isEmpty();
            assertThat(stored.getCreatedAt()).isEqualTo(creation);
            assertThat(stored.getUpdatedAt()).isEqualTo(creation);

            auditingHandler.setDateTimeProvider(() -> Optional.of(addition));
            DownloadRule rule = stored.addDownloadRule(".EXE");
            entityManager.flush();
            Long ruleId = rule.getId();
            entityManager.clear();

            Policy afterAddition = entityManager.find(Policy.class, policyId);
            assertThat(afterAddition.getCreatedAt()).isEqualTo(creation);
            assertThat(afterAddition.getUpdatedAt()).isEqualTo(addition);
            assertThat(afterAddition.getDownloadRules()).extracting(DownloadRule::getExtension)
                    .containsExactly("exe");
            entityManager.clear();

            // Cargar directamente la regla comprueba también su relación LAZY.
            auditingHandler.setDateTimeProvider(() -> Optional.of(edition));
            entityManager.find(DownloadRule.class, ruleId).changeExtension(".PDF");
            entityManager.flush();
            entityManager.clear();

            Policy afterEdition = entityManager.find(Policy.class, policyId);
            assertThat(afterEdition.getCreatedAt()).isEqualTo(creation);
            assertThat(afterEdition.getUpdatedAt()).isEqualTo(edition);
            assertThat(afterEdition.getDownloadRules()).extracting(DownloadRule::getExtension)
                    .containsExactly("pdf");

            auditingHandler.setDateTimeProvider(() -> Optional.of(removal));
            afterEdition.removeDownloadRule(afterEdition.getDownloadRules().getFirst());
            entityManager.flush();
            entityManager.clear();

            Policy afterRemoval = entityManager.find(Policy.class, policyId);
            assertThat(afterRemoval.getCreatedAt()).isEqualTo(creation);
            assertThat(afterRemoval.getUpdatedAt()).isEqualTo(removal);
            assertThat(afterRemoval.getDownloadRules()).isEmpty();
            assertThat(afterRemoval.getUrlRules()).extracting(UrlRule::getDomain)
                    .containsExactly("example.com");
            assertThat(entityManager.find(DownloadRule.class, ruleId)).isNull();
        } finally {
            auditingHandler.setDateTimeProvider(CurrentDateTimeProvider.INSTANCE);
        }
    }

    /** PostgreSQL impide duplicados aunque se evite la validación Java. */
    @Test
    void databaseRejectsDuplicateExtensionWithinPolicy() {
        Policy policy = persistPolicy("exe");

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "INSERT INTO download_rules (policy_id, extension) VALUES (:policyId, :extension)")
                .setParameter("policyId", policy.getId())
                .setParameter("extension", "exe")
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("uq_download_rules_policy_extension");
    }

    /** La misma extensión puede guardarse en políticas distintas. */
    @Test
    void databaseAllowsSameExtensionInDifferentPolicies() {
        Long firstId = persistPolicy("exe").getId();
        Long secondId = persistPolicy(".EXE").getId();
        entityManager.clear();

        DownloadRule first = entityManager.find(Policy.class, firstId).getDownloadRules().getFirst();
        DownloadRule second = entityManager.find(Policy.class, secondId).getDownloadRules().getFirst();

        assertThat(first.getId()).isPositive().isNotEqualTo(second.getId());
        assertThat(first.getExtension()).isEqualTo(second.getExtension());
        assertThat(first.getPolicy().getId()).isNotEqualTo(second.getPolicy().getId());
    }

    /** Comprueba el límite de la columna con caracteres simples y Unicode. */
    @ParameterizedTest
    @ValueSource(strings = {"a", "ñ", "😀"})
    void databaseRespectsExtensionLengthLimit(String symbol) {
        String maximum = symbol.repeat(20);
        Long policyId = persistPolicy("." + maximum).getId();
        entityManager.clear();
        DownloadRule rule = entityManager.find(Policy.class, policyId).getDownloadRules().getFirst();
        assertThat(rule.getExtension()).isEqualTo(maximum);

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE download_rules SET extension = :extension WHERE id = :id")
                .setParameter("extension", maximum + symbol)
                .setParameter("id", rule.getId())
                .executeUpdate())
                .isInstanceOf(DataException.class)
                .satisfies(error -> assertThat(((DataException) error).getSQLException().getSQLState())
                        .isEqualTo("22001"));
    }

    /** El CHECK rechaza extensiones sin normalizar y espacios ASCII o Unicode. */
    @ParameterizedTest
    @ValueSource(strings = {
            "", " ", "EXE", ".exe", "tar.gz", "e xe", "e\txe",
            "e\u00A0xe", "e\u2003xe", "e\u202Fxe", "e\u3000xe", "e\u0085xe",
            "folder/exe", "folder\\exe", "C:exe"
    })
    void databaseRejectsInvalidOrUnnormalizedExtension(String extension) {
        Long ruleId = persistPolicy("exe").getDownloadRules().getFirst().getId();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE download_rules SET extension = :extension WHERE id = :id")
                .setParameter("extension", extension)
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("ck_download_rules_extension");
    }

    /** PostgreSQL exige la política y la extensión de cada regla. */
    @ParameterizedTest
    @ValueSource(strings = {"policy_id", "extension"})
    void databaseRejectsMissingRequiredValue(String column) {
        Long ruleId = persistPolicy("exe").getDownloadRules().getFirst().getId();

        // La columna procede únicamente de los dos casos fijos de esta prueba.
        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE download_rules SET " + column + " = NULL WHERE id = :id")
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining(column);
    }

    /** Comprueba que la clave foránea rechaza una política inexistente. */
    @Test
    void databaseRejectsUnknownPolicy() {
        Long ruleId = persistPolicy("exe").getDownloadRules().getFirst().getId();

        assertThatThrownBy(() -> entityManager.createNativeQuery(
                "UPDATE download_rules SET policy_id = :policyId WHERE id = :id")
                .setParameter("policyId", Long.MAX_VALUE)
                .setParameter("id", ruleId)
                .executeUpdate())
                .isInstanceOf(ConstraintViolationException.class)
                .hasMessageContaining("fk_download_rules_policy");
    }

    /** JPA elimina ambas colecciones con Policy y conserva Company. */
    @Test
    void deletingPolicyByJpaDeletesBothRuleCollectionsAndKeepsCompany() {
        Policy policy = persistPolicy("exe");
        UrlRule urlRule = policy.addUrlRule("example.com");
        entityManager.flush();
        Long policyId = policy.getId();
        Long companyId = policy.getCompany().getId();
        Long downloadRuleId = policy.getDownloadRules().getFirst().getId();
        Long urlRuleId = urlRule.getId();

        entityManager.remove(policy);
        entityManager.flush();
        entityManager.clear();

        assertThat(entityManager.find(Policy.class, policyId)).isNull();
        assertThat(entityManager.find(DownloadRule.class, downloadRuleId)).isNull();
        assertThat(entityManager.find(UrlRule.class, urlRuleId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** El borrado SQL de Policy elimina sus reglas mediante la clave foránea. */
    @Test
    void databaseCascadesPolicyDeletionAndKeepsCompany() {
        Policy policy = persistPolicy("exe");
        Long policyId = policy.getId();
        Long ruleId = policy.getDownloadRules().getFirst().getId();
        Long companyId = policy.getCompany().getId();

        entityManager.createNativeQuery("DELETE FROM policies WHERE id = :id")
                .setParameter("id", policyId)
                .executeUpdate();
        entityManager.clear();

        assertThat(entityManager.find(Policy.class, policyId)).isNull();
        assertThat(entityManager.find(DownloadRule.class, ruleId)).isNull();
        assertThat(entityManager.find(Company.class, companyId)).isNotNull();
    }

    /** Crea una política con una compañía ya guardada para las relaciones. */
    private Policy createPolicy() {
        Company company = new Company("Empresa A");
        entityManager.persist(company);
        return new Policy("Política A", company);
    }

    /** Guarda una política y sus extensiones mediante la cascada de JPA. */
    private Policy persistPolicy(String... extensions) {
        Policy policy = createPolicy();
        for (String extension : extensions) {
            policy.addDownloadRule(extension);
        }
        entityManager.persist(policy);
        entityManager.flush();
        return policy;
    }
}
