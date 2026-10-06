package com.securebrowser.platform.company;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.junit.jupiter.params.provider.NullAndEmptySource;

/**
 * Comprueba las reglas del nombre de una compañía.
 */
class CompanyTest {

    /**
     * Comprueba que se eliminan los espacios exteriores
     * y se conserva el contenido del nombre.
     */
    @Test
    void constructorNormalizesName() {
        Company company = new Company("  Empresa  Álamo  ");

        assertThat(company.getName()).isEqualTo("Empresa  Álamo");
    }

    /**
     * Comprueba que una compañía no admite un nombre nulo.
     */
    @Test
    void constructorRejectsNullName() {
        assertThatThrownBy(() -> new Company(null))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /**
     * Comprueba que una compañía rechaza nombres vacíos
     * o formados únicamente por espacios en blanco.
     */
    @ParameterizedTest
    @ValueSource(strings = { "", "   ", "\t\n", "\u2003" })
    void constructorRejectsBlankName(String name) {
        assertThatThrownBy(() -> new Company(name))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /**
     * Comprueba el límite de 150 puntos de código
     * después de eliminar los espacios exteriores.
     */
    @ParameterizedTest
    @ValueSource(strings = { "A", "😀" })
    void constructorRespectsNameLengthLimit(String symbol) {
        String maximumLengthName = symbol.repeat(150);

        Company company = new Company("  " + maximumLengthName + "  ");

        assertThat(company.getName()).isEqualTo(maximumLengthName);

        assertThatThrownBy(() -> new Company(maximumLengthName + symbol))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /**
     * Comprueba que el cambio normaliza el nombre y conserva
     * el último nombre válido cuando se rechaza otro.
     */
    @Test
    void changeNameValidatesBeforeReplacingCurrentName() {
        Company company = new Company("Empresa A");

        company.changeName("  Empresa B  ");

        assertThat(company.getName()).isEqualTo("Empresa B");

        String tooLongName = "A".repeat(151);

        assertThatThrownBy(() -> company.changeName(tooLongName))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(company.getName()).isEqualTo("Empresa B");
    }

    /**
     * Comprueba que un nombre nulo o en blanco se rechaza
     * sin modificar el nombre actual de la compañía.
     */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = { "   ", "\t\n", "\u2003" })
    void changeNameRejectsMissingNameWithoutChangingCurrentName(String name) {
        Company company = new Company("Empresa A");

        assertThatThrownBy(() -> company.changeName(name))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(company.getName()).isEqualTo("Empresa A");
    }
}