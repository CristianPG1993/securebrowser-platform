package com.securebrowser.platform.license;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.securebrowser.platform.company.Company;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;

/**
 * Comprueba las reglas de capacidad y pertenencia de License.
 */
class LicenseTest {

    /** Admite capacidades positivas y conserva la compañía recibida. */
    @ParameterizedTest
    @ValueSource(ints = {1, 10, Integer.MAX_VALUE})
    void constructorAcceptsPositiveCapacity(int capacity) {
        Company company = new Company("Empresa A");
        License license = new License(company, capacity);

        assertThat(license.getCompany()).isSameAs(company);
        assertThat(license.getMaxInstallations()).isEqualTo(capacity);
    }

    /** Rechaza una capacidad inicial inferior a uno. */
    @ParameterizedTest
    @ValueSource(ints = {Integer.MIN_VALUE, -1, 0})
    void constructorRejectsInvalidCapacity(int capacity) {
        assertThatThrownBy(() -> new License(new Company("Empresa A"), capacity))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Exige una compañía al crear la licencia. */
    @Test
    void constructorRequiresCompany() {
        assertThatThrownBy(() -> new License(null, 10))
                .isInstanceOf(IllegalArgumentException.class);
    }

    /** Admite cambios positivos de capacidad y conserva la compañía. */
    @Test
    void changesCapacityWithoutChangingCompany() {
        Company company = new Company("Empresa A");
        License license = new License(company, 10);

        license.changeMaxInstallations(20);
        assertThat(license.getMaxInstallations()).isEqualTo(20);

        license.changeMaxInstallations(1);
        assertThat(license.getMaxInstallations()).isEqualTo(1);
        assertThat(license.getCompany()).isSameAs(company);
    }

    /** Conserva la capacidad anterior cuando se rechaza un cambio. */
    @ParameterizedTest
    @ValueSource(ints = {Integer.MIN_VALUE, -1, 0})
    void rejectsInvalidChangeWithoutReplacingCapacity(int capacity) {
        License license = new License(new Company("Empresa A"), 10);

        assertThatThrownBy(() -> license.changeMaxInstallations(capacity))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(license.getMaxInstallations()).isEqualTo(10);
    }
}
