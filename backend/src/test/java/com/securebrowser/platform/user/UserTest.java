package com.securebrowser.platform.user;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.securebrowser.platform.company.Company;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/**
 * Comprueba las validaciones y modificaciones de User.
 */
class UserTest {

    // Hashes de prueba; no corresponden a credenciales del proyecto.
    private static final String PASSWORD_HASH = "$2a$10$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    private static final String OTHER_PASSWORD_HASH = "$2b$12$N9qo8uLOickgx2ZMRZoMyeIjZAgcfl7p92ldGxad68LJZdL17lhWy";

    /** Comprueba la normalización y las asociaciones iniciales. */
    @Test
    void constructorNormalizesUserData() {
        Company company = new Company("Empresa A");

        User user = new User(
                "  Ana  ", "  García  ", "  ANA@EXAMPLE.COM  ",
                PASSWORD_HASH, UserRole.USER, company);

        assertThat(user.getName()).isEqualTo("Ana");
        assertThat(user.getLastName()).isEqualTo("García");
        assertThat(user.getEmail()).isEqualTo("ana@example.com");
        assertThat(user.getPasswordHash()).isEqualTo(PASSWORD_HASH);
        assertThat(user.getRole()).isEqualTo(UserRole.USER);
        assertThat(user.getCompany()).isSameAs(company);
    }

    /** Rechaza nombres y apellidos ausentes sin modificar los anteriores. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = { "   ", "\t\n", "\u2003" })
    void rejectsMissingNames(String value) {
        Company company = new Company("Empresa A");

        assertThatThrownBy(() -> new User(
                value, "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> new User(
                "Ana", value, "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        User user = createUser();

        assertThatThrownBy(() -> user.changeName(value))
                .isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> user.changeLastName(value))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getName()).isEqualTo("Ana");
        assertThat(user.getLastName()).isEqualTo("García");
    }

    /** Comprueba el límite de nombres con caracteres simples y Unicode. */
    @ParameterizedTest
    @ValueSource(strings = { "A", "😀" })
    void respectsNameLengthLimits(String symbol) {
        String maximum = symbol.repeat(150);
        Company company = new Company("Empresa A");

        User user = new User(
                "  " + maximum + "  ", maximum, "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company);

        assertThat(user.getName()).isEqualTo(maximum);
        assertThat(user.getLastName()).isEqualTo(maximum);

        String tooLong = maximum + symbol;

        assertThatThrownBy(() -> new User(
                tooLong, "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> new User(
                "Ana", tooLong, "ana@example.com",
                PASSWORD_HASH, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> user.changeName(tooLong))
                .isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> user.changeLastName(tooLong))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getName()).isEqualTo(maximum);
        assertThat(user.getLastName()).isEqualTo(maximum);
    }

    /** Rechaza emails inválidos y conserva el anterior al modificarlos. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {
            "   ",
            "ana",
            "ana@",
            "@example.com",
            "ana@@example.com",
            "ana @example.com",
            ".ana@example.com",
            "ana..garcia@example.com",
            "ana@-example.com",
            "ana@example..com"
    })
    void rejectsInvalidEmails(String email) {
        Company company = new Company("Empresa A");

        assertThatThrownBy(() -> new User(
                "Ana", "García", email,
                PASSWORD_HASH, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        User user = createUser();

        assertThatThrownBy(() -> user.changeEmail(email))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getEmail()).isEqualTo("ana@example.com");
    }

    /** Admite un email de 254 caracteres y rechaza uno de 255. */
    @Test
    void respectsEmailLengthLimit() {
        String maximumEmail = "a".repeat(64) + "@"
                + "b".repeat(63) + "."
                + "c".repeat(63) + "."
                + "d".repeat(61);

        User user = createUser();
        user.changeEmail("  " + maximumEmail + "  ");

        assertThat(user.getEmail()).isEqualTo(maximumEmail);
        assertThat(user.getEmail()).hasSize(254);

        assertThatThrownBy(() -> user.changeEmail(maximumEmail + "d"))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getEmail()).isEqualTo(maximumEmail);
    }

    /** Rechaza valores que no tienen formato BCrypt. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = { "   ", "password123", "$2a$10$incomplete" })
    void rejectsInvalidPasswordHashes(String hash) {
        Company company = new Company("Empresa A");

        assertThatThrownBy(() -> new User(
                "Ana", "García", "ana@example.com",
                hash, UserRole.USER, company)).isInstanceOf(IllegalArgumentException.class);

        User user = createUser();

        assertThatThrownBy(() -> user.changePasswordHash(hash))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getPasswordHash()).isEqualTo(PASSWORD_HASH);
    }

    /** Exige compañía y rol al crear el usuario. */
    @Test
    void constructorRequiresCompanyAndRole() {
        assertThatThrownBy(() -> new User(
                "Ana", "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, null)).isInstanceOf(IllegalArgumentException.class);

        assertThatThrownBy(() -> new User(
                "Ana", "García", "ana@example.com",
                PASSWORD_HASH, null, new Company("Empresa A"))).isInstanceOf(IllegalArgumentException.class);
    }

    /** Comprueba cambios válidos y conserva la compañía de origen. */
    @Test
    void changesUserData() {
        User user = createUser();
        Company originalCompany = user.getCompany();

        user.changeName("  Luis  ");
        user.changeLastName("  Pérez  ");
        user.changeEmail("  LUIS@EXAMPLE.COM  ");
        user.changePasswordHash(OTHER_PASSWORD_HASH);
        user.changeRole(UserRole.ADMIN);

        assertThat(user.getName()).isEqualTo("Luis");
        assertThat(user.getLastName()).isEqualTo("Pérez");
        assertThat(user.getEmail()).isEqualTo("luis@example.com");
        assertThat(user.getPasswordHash()).isEqualTo(OTHER_PASSWORD_HASH);
        assertThat(user.getRole()).isEqualTo(UserRole.ADMIN);
        assertThat(user.getCompany()).isSameAs(originalCompany);
    }

    /** Rechaza un rol nulo sin sustituir el anterior. */
    @Test
    void changeRoleRejectsNullWithoutChangingCurrentRole() {
        User user = createUser();

        assertThatThrownBy(() -> user.changeRole(null))
                .isInstanceOf(IllegalArgumentException.class);

        assertThat(user.getRole()).isEqualTo(UserRole.USER);
    }

    /** Crea un usuario válido para las pruebas de modificación. */
    private static User createUser() {
        return new User(
                "Ana", "García", "ana@example.com",
                PASSWORD_HASH, UserRole.USER, new Company("Empresa A"));
    }
}