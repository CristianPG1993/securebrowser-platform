package com.securebrowser.platform.policy;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.securebrowser.platform.company.Company;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/**
 * Comprueba el nombre, la compañía y la configuración base de Policy.
 */
class PolicyTest {

    /** Comprueba la normalización y los valores iniciales de las funcionalidades. */
    @Test
    void constructorNormalizesNameAndUsesDefaultConfiguration() {
        Company company = new Company("Empresa A");
        Policy policy = new Policy("  Política  Álamo  ", company);

        assertThat(policy.getName()).isEqualTo("Política  Álamo");
        assertThat(policy.getCompany()).isSameAs(company);
        assertThat(policy.isUrlFilteringEnabled()).isTrue();
        assertThat(policy.getUrlFilteringMode()).isEqualTo(FilterMode.DENYLIST);
        assertThat(policy.isDownloadControlEnabled()).isTrue();
        assertThat(policy.getDownloadControlMode()).isEqualTo(FilterMode.DENYLIST);
    }

    /** Rechaza un nombre ausente sin sustituir un nombre previo válido. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {"   ", "\t\n", "\u2003"})
    void rejectsMissingName(String name) {
        assertThatThrownBy(() -> new Policy(name, new Company("Empresa A")))
                .isInstanceOf(IllegalArgumentException.class);

        Policy policy = createPolicy();

        assertThatThrownBy(() -> policy.changeName(name))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(policy.getName()).isEqualTo("Política A");
    }

    /** Comprueba el límite de 150 caracteres simples y Unicode. */
    @ParameterizedTest
    @ValueSource(strings = {"A", "😀"})
    void respectsNameLengthLimit(String symbol) {
        String maximum = symbol.repeat(150);
        Company company = new Company("Empresa A");
        Policy policy = new Policy("  " + maximum + "  ", company);

        assertThat(policy.getName()).isEqualTo(maximum);

        String tooLong = maximum + symbol;

        assertThatThrownBy(() -> new Policy(tooLong, company))
                .isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> policy.changeName(tooLong))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(policy.getName()).isEqualTo(maximum);
    }

    /** Exige una compañía al crear la política. */
    @Test
    void constructorRequiresCompany() {
        assertThatThrownBy(() -> new Policy("Política A", null))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Cambia y normaliza el nombre conservando la compañía. */
    @Test
    void changesNameWithoutChangingCompany() {
        Policy policy = createPolicy();
        Company company = policy.getCompany();

        policy.changeName("  Política B  ");

        assertThat(policy.getName()).isEqualTo("Política B");
        assertThat(policy.getCompany()).isSameAs(company);
    }

    /** Configura URLs con ambos modos sin alterar el control de descargas. */
    @ParameterizedTest
    @EnumSource(FilterMode.class)
    void configuresUrlFilteringIndependently(FilterMode mode) {
        Policy policy = createPolicy();

        policy.configureUrlFiltering(false, mode);

        assertThat(policy.isUrlFilteringEnabled()).isFalse();
        assertThat(policy.getUrlFilteringMode()).isEqualTo(mode);
        assertThat(policy.isDownloadControlEnabled()).isTrue();
        assertThat(policy.getDownloadControlMode()).isEqualTo(FilterMode.DENYLIST);

        policy.configureUrlFiltering(true, mode);

        assertThat(policy.isUrlFilteringEnabled()).isTrue();
        assertThat(policy.getUrlFilteringMode()).isEqualTo(mode);
    }

    /** Configura descargas con ambos modos sin alterar el filtrado de URLs. */
    @ParameterizedTest
    @EnumSource(FilterMode.class)
    void configuresDownloadControlIndependently(FilterMode mode) {
        Policy policy = createPolicy();

        policy.configureDownloadControl(false, mode);

        assertThat(policy.isDownloadControlEnabled()).isFalse();
        assertThat(policy.getDownloadControlMode()).isEqualTo(mode);
        assertThat(policy.isUrlFilteringEnabled()).isTrue();
        assertThat(policy.getUrlFilteringMode()).isEqualTo(FilterMode.DENYLIST);

        policy.configureDownloadControl(true, mode);

        assertThat(policy.isDownloadControlEnabled()).isTrue();
        assertThat(policy.getDownloadControlMode()).isEqualTo(mode);
    }

    /** Conserva la configuración de URLs cuando se rechaza un modo nulo. */
    @Test
    void rejectsNullUrlModeWithoutChangingConfiguration() {
        Policy policy = createPolicy();
        policy.configureUrlFiltering(false, FilterMode.ALLOWLIST);

        assertThatThrownBy(() -> policy.configureUrlFiltering(true, null))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(policy.isUrlFilteringEnabled()).isFalse();
        assertThat(policy.getUrlFilteringMode()).isEqualTo(FilterMode.ALLOWLIST);
    }

    /** Conserva la configuración de descargas cuando se rechaza un modo nulo. */
    @Test
    void rejectsNullDownloadModeWithoutChangingConfiguration() {
        Policy policy = createPolicy();
        policy.configureDownloadControl(false, FilterMode.ALLOWLIST);

        assertThatThrownBy(() -> policy.configureDownloadControl(true, null))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(policy.isDownloadControlEnabled()).isFalse();
        assertThat(policy.getDownloadControlMode()).isEqualTo(FilterMode.ALLOWLIST);
    }

    /** Crea una política válida para las pruebas de modificación. */
    private static Policy createPolicy() {
        return new Policy("Política A", new Company("Empresa A"));
    }
}
