package com.securebrowser.platform.company.dto;

import static org.assertj.core.api.Assertions.assertThat;

import jakarta.validation.Validation;
import jakarta.validation.Validator;
import jakarta.validation.ValidatorFactory;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;

/** Comprueba la normalización y validación del nombre recibido. */
class CompanyUpdateRequestTest {

    /** Acepta un nombre válido después de eliminar los espacios exteriores. */
    @Test
    void acceptsAndNormalizesValidName() {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();

            CompanyUpdateRequest request = new CompanyUpdateRequest("  Empresa A  ");

            assertThat(request.name()).isEqualTo("Empresa A");
            assertThat(validator.validate(request)).isEmpty();
        }
    }

    /** Rechaza nombres nulos, vacíos o formados solo por espacios. */
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings = {"   ", "\t\n", "\u2003"})
    void rejectsMissingOrBlankName(String name) {
        try (ValidatorFactory factory =
                Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();

            CompanyUpdateRequest request = new CompanyUpdateRequest(name);

            assertThat(validator.validate(request)).hasSize(1);
        }
    }

    /** Acepta 150 puntos de código después de recortar espacios Unicode. */
    @ParameterizedTest
    @ValueSource(strings = { "A", "\uD83D\uDE00" })
    void acceptsMaximumLengthAfterNormalization(String symbol) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();

            String name = symbol.repeat(150);
            CompanyUpdateRequest request = new CompanyUpdateRequest("\u2003" + name + "\u2003");

            assertThat(request.name()).isEqualTo(name);
            assertThat(validator.validate(request)).isEmpty();
        }
    }

    /** Rechaza nombres que superan los 150 puntos de código. */
    @ParameterizedTest
    @ValueSource(strings = { "A", "\uD83D\uDE00" })
    void rejectsNameExceedingMaximumLength(String symbol) {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();

            CompanyUpdateRequest request = new CompanyUpdateRequest(symbol.repeat(151));

            assertThat(validator.validate(request)).hasSize(1);
        }
    }
}