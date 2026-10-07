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

/** Comprueba las extensiones y la gestión de las reglas de descarga de Policy. */
class DownloadRuleTest {

    /** Normaliza mayúsculas y un punto inicial conservando la política. */
    @ParameterizedTest
    @CsvSource({".EXE, exe", "PDF, pdf", ".7Z, 7z", ".Ñ, ñ", "C++, c++"})
    void normalizesExtensionAndMaintainsAssociation(String input, String expected) {
        Policy policy = createPolicy();
        assertThat(policy.getDownloadRules()).isEmpty();

        DownloadRule rule = policy.addDownloadRule(input);

        assertThat(rule.getId()).isNull();
        assertThat(rule.getExtension()).isEqualTo(expected);
        assertThat(rule.getPolicy()).isSameAs(policy);
        assertThat(policy.getDownloadRules()).containsExactly(rule);
        assertThat(policy.getUpdatedAt()).isNotNull();
    }

    /** Rechaza entradas inválidas sin alterar una regla ni su colección. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {
            " ", "\t\n", ".", "..exe", "tar.gz", ".tar.gz", "exe.",
            " exe", "exe ", "e xe", "e\u00A0xe", "e\u2003xe",
            "e\u202Fxe", "e\u3000xe", "e\u0001xe", "e\u0085xe",
            "/exe", "folder/exe", "folder\\exe", "C:exe"
    })
    void rejectsInvalidExtensionWithoutChangingState(String extension) {
        Policy policy = createPolicy();
        DownloadRule rule = policy.addDownloadRule("exe");
        Instant updatedAt = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.addDownloadRule(extension))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> rule.changeExtension(extension))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(rule.getExtension()).isEqualTo("exe");
        assertThat(policy.getDownloadRules()).containsExactly(rule);
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Comprueba los 20 caracteres después de normalizar, también con Unicode. */
    @ParameterizedTest
    @ValueSource(strings = {"a", "ñ", "😀"})
    void respectsExtensionLengthLimit(String symbol) {
        Policy policy = createPolicy();
        String maximum = symbol.repeat(20);
        DownloadRule rule = policy.addDownloadRule("." + maximum);

        assertThat(rule.getExtension()).isEqualTo(maximum);
        assertThatThrownBy(() -> policy.addDownloadRule(maximum + symbol))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> rule.changeExtension(maximum + symbol))
                .isInstanceOf(IllegalArgumentException.class);
        assertThat(rule.getExtension()).isEqualTo(maximum);
    }

    /** Exige una política de origen. */
    @Test
    void requiresPolicy() {
        assertThatThrownBy(() -> new DownloadRule(null, "exe"))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Rechaza duplicados normalizados al añadir y al modificar. */
    @Test
    void rejectsDuplicateExtensionWithinPolicy() {
        Policy policy = createPolicy();
        DownloadRule first = policy.addDownloadRule("exe");
        DownloadRule second = policy.addDownloadRule("pdf");
        Instant updatedAt = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.addDownloadRule(".EXE"))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> second.changeExtension(".EXE"))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(second.getExtension()).isEqualTo("pdf");
        assertThat(policy.getDownloadRules()).containsExactly(first, second);
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Permite la misma extensión en políticas distintas. */
    @Test
    void allowsSameExtensionInDifferentPolicies() {
        DownloadRule first = createPolicy().addDownloadRule("exe");
        DownloadRule second = createPolicy().addDownloadRule(".EXE");

        assertThat(second.getExtension()).isEqualTo(first.getExtension());
        assertThat(second.getPolicy()).isNotSameAs(first.getPolicy());
    }

    /** Un cambio normalizado equivalente conserva la fecha de modificación. */
    @Test
    void normalizedSameExtensionDoesNotChangeUpdatedAt() {
        Policy policy = createPolicy();
        DownloadRule rule = policy.addDownloadRule("exe");
        Instant updatedAt = policy.getUpdatedAt();

        rule.changeExtension(".EXE");

        assertThat(rule.getExtension()).isEqualTo("exe");
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Impide alterar la lista directamente y retirar reglas de otra política. */
    @Test
    void protectsCollectionAndRejectsForeignRule() {
        Policy policy = createPolicy();
        DownloadRule own = policy.addDownloadRule("exe");
        DownloadRule foreign = createPolicy().addDownloadRule("pdf");
        Instant updatedAt = policy.getUpdatedAt();

        assertThatThrownBy(() -> policy.getDownloadRules().clear())
                .isInstanceOf(UnsupportedOperationException.class);
        assertThatThrownBy(() -> policy.removeDownloadRule(foreign))
                .isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(() -> policy.removeDownloadRule(null))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(policy.getDownloadRules()).containsExactly(own);
        assertThat(policy.getUpdatedAt()).isEqualTo(updatedAt);
    }

    /** Edita y retira descargas sin alterar URLs ni sus modos de configuración. */
    @Test
    void downloadRuleChangesPreservePolicyAndUrlRules() {
        Policy policy = createPolicy();
        policy.configureDownloadControl(false, FilterMode.ALLOWLIST);
        UrlRule urlRule = policy.addUrlRule("example.com");
        DownloadRule rule = policy.addDownloadRule("exe");

        rule.changeExtension(".PDF");

        assertThat(rule.getExtension()).isEqualTo("pdf");
        assertThat(rule.getPolicy()).isSameAs(policy);
        assertThat(policy.getDownloadRules()).containsExactly(rule);

        policy.removeDownloadRule(rule);

        assertThat(policy.getDownloadRules()).isEmpty();
        assertThat(rule.getPolicy()).isSameAs(policy);
        assertThat(policy.getUrlRules()).containsExactly(urlRule);
        assertThat(policy.isDownloadControlEnabled()).isFalse();
        assertThat(policy.getDownloadControlMode()).isEqualTo(FilterMode.ALLOWLIST);
        assertThat(policy.getUrlFilteringMode()).isEqualTo(FilterMode.DENYLIST);
    }

    /** Crea una política válida sin conectar con PostgreSQL. */
    private static Policy createPolicy() {
        return new Policy("Política A", new Company("Empresa A"));
    }
}
