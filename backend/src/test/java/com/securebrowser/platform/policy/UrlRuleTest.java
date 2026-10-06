package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import java.time.Instant;

import com.securebrowser.platform.company.Company;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba la normalización de dominios y la gestión de reglas de Policy. */
class UrlRuleTest {

    /** Normaliza espacios exteriores, mayúsculas, punto final y dominios IDN. */
    @ParameterizedTest
    @CsvSource({
            "'  EXAMPLE.COM.  ', example.com",
            "bücher.de, xn--bcher-kva.de",
            "XN--BCHER-KVA.DE., xn--bcher-kva.de",
            "example。com。, example.com",
            "sub-domain.example.com, sub-domain.example.com"
    })
    void normalizesDomainAndMaintainsAssociation(String input, String expected) {
        Policy policy = createPolicy();

        assertThat(policy.getUrlRules()).isEmpty();

        UrlRule rule = policy.addUrlRule(input);

        assertThat(rule.getId()).isNull();
        assertThat(rule.getDomain()).isEqualTo(expected);
        assertThat(rule.getPolicy()).isSameAs(policy);
        assertThat(policy.getUrlRules()).containsExactly(rule);
        assertThat(policy.getUpdatedAt()).isNotNull();
    }

    /** Rechaza entradas inválidas sin alterar la regla ni la colección. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {
            "   ", "\t\n", "\u2003", ".", "example.com..",
            "https://example.com", "example.com/path", "example.com:443",
            "*.example.com", "example..com", "-example.com", "example-.com",
            "exam_ple.com", "example com", "example.com?query=1", "user@example.com"
    })
    void rejectsInvalidDomainWithoutChangingState(String domain) {
        Policy policy = createPolicy();
        UrlRule rule = policy.addUrlRule("example.com");
        Instant updatedAt = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.addUrlRule(domain))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> rule.changeDomain(domain))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(rule.getDomain()).isEqualTo("example.com");
        assertThat(policy.getUrlRules()).containsExactly(rule);
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Comprueba el límite total y el máximo de 63 caracteres por etiqueta. */
    @Test
    void respectsDomainAndLabelLengthLimits() {
        String maximum = "a".repeat(63) + "." + "b".repeat(63)
                + "." + "c".repeat(63) + "." + "d".repeat(61);
        Policy policy = createPolicy();
        UrlRule rule = policy.addUrlRule(maximum + ".");

        assertThat(rule.getDomain()).hasSize(253).isEqualTo(maximum);
        assertThatThrownBy(() -> rule.changeDomain(maximum + "d"))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> policy.addUrlRule("a".repeat(64) + ".com"))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(rule.getDomain()).isEqualTo(maximum);
    }

    /** Exige una política de origen. */
    @Test
    void requiresPolicy() {
        assertThatThrownBy(() -> new UrlRule(null, "example.com"))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Rechaza duplicados normalizados al añadir y al modificar una regla. */
    @Test
    void rejectsDuplicateDomainWithinPolicy() {
        Policy policy = createPolicy();
        UrlRule first = policy.addUrlRule("bücher.de");
        UrlRule second = policy.addUrlRule("example.com");
        Instant updatedAt = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.addUrlRule("  XN--BCHER-KVA.DE.  "))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> second.changeDomain("BÜCHER.DE."))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(second.getDomain()).isEqualTo("example.com");
        assertThat(policy.getUrlRules()).containsExactly(first, second);
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Admite el mismo dominio en políticas diferentes. */
    @Test
    void allowsSameDomainInDifferentPolicies() {
        UrlRule first = createPolicy().addUrlRule("example.com");
        UrlRule second = createPolicy().addUrlRule("EXAMPLE.COM.");

        assertThat(second.getDomain()).isEqualTo(first.getDomain());
        assertThat(second.getPolicy()).isNotSameAs(first.getPolicy());
    }

    /** Cambia un dominio y conserva la política de origen. */
    @Test
    void changesDomainWithoutChangingPolicy() {
        Policy policy = createPolicy();
        UrlRule rule = policy.addUrlRule("example.com");
        Instant before = policy.getUpdatedAt();

        rule.changeDomain("  BÜCHER.DE.  ");

        assertThat(rule.getDomain()).isEqualTo("xn--bcher-kva.de");
        assertThat(rule.getPolicy()).isSameAs(policy);
        assertThat(policy.getUrlRules()).containsExactly(rule);
        assertThat(policy.getUpdatedAt()).isAfterOrEqualTo(before);
    }

    /** Un dominio que ya coincide no marca la política como modificada. */
    @Test
    void normalizedSameDomainDoesNotChangeUpdatedAt() {
        Policy policy = createPolicy();
        UrlRule rule = policy.addUrlRule("example.com");
        Instant before = policy.getUpdatedAt();

        rule.changeDomain("  EXAMPLE.COM.  ");

        assertThat(policy.getUpdatedAt()).isEqualTo(before);
    }

    /** Impide modificar la colección saltándose los métodos de Policy. */
    @Test
    void exposesReadOnlyCollection() {
        Policy policy = createPolicy();
        UrlRule rule = policy.addUrlRule("example.com");

        assertThatThrownBy(() -> policy.getUrlRules().clear())
                .isInstanceOf(UnsupportedOperationException.class);
        assertThat(policy.getUrlRules()).containsExactly(rule);
    }

    /** Solo permite retirar reglas propias y conserva su política de origen. */
    @Test
    void removesOwnRuleAndRejectsForeignRule() {
        Policy policy = createPolicy();
        UrlRule own = policy.addUrlRule("example.com");
        UrlRule foreign = createPolicy().addUrlRule("other.com");
        Instant before = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.removeUrlRule(foreign))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(policy.getUrlRules()).containsExactly(own);
        assertThat(policy.getUpdatedAt()).isEqualTo(before);

        policy.removeUrlRule(own);

        assertThat(policy.getUrlRules()).isEmpty();
        assertThat(own.getPolicy()).isSameAs(policy);
        assertThat(policy.getUpdatedAt()).isAfterOrEqualTo(before);
    }

    /** Crea una política válida sin acceder a la base de datos. */
    private static Policy createPolicy() {
        return new Policy("Política A", new Company("Empresa A"));
    }
}
