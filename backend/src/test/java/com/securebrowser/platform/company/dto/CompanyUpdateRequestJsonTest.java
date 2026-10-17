package com.securebrowser.platform.company.dto;

import static org.assertj.core.api.Assertions.assertThat;

import jakarta.validation.Validation;
import jakarta.validation.Validator;
import jakarta.validation.ValidatorFactory;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.json.JsonTest;
import org.springframework.boot.test.json.JacksonTester;

/** Comprueba la lectura y validación del JSON de actualización. */
@JsonTest
class CompanyUpdateRequestJsonTest {

    // Convierte el JSON recibido al DTO de entrada.
    @Autowired
    private JacksonTester<CompanyUpdateRequest> json;

    /** Rechaza la ausencia del nombre y su valor nulo explícito. */
    @ParameterizedTest
    @ValueSource(strings = { "{}", "{\"name\":null}" })
    void rejectsMissingOrNullNameAfterDeserialization(String content)
            throws Exception {
        try (ValidatorFactory factory = Validation.buildDefaultValidatorFactory()) {
            Validator validator = factory.getValidator();

            CompanyUpdateRequest request = json.parseObject(content);

            assertThat(validator.validate(request)).hasSize(1);
        }
    }
}