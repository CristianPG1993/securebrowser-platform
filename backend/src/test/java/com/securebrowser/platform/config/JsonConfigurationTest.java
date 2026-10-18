package com.securebrowser.platform.config;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;

import com.securebrowser.platform.company.dto.CompanyUpdateRequest;

import tools.jackson.core.JacksonException;
import tools.jackson.databind.json.JsonMapper;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.autoconfigure.json.JsonTest;
import org.springframework.context.annotation.Import;

/** Comprueba las reglas de lectura JSON utilizando la configuración real de Jackson. */
@JsonTest
@Import(JsonConfiguration.class)
class JsonConfigurationTest {

    // Mapper preparado por Spring Boot con las reglas de JsonConfiguration.
    @Autowired
    private JsonMapper mapper;

    /** Acepta nombres textuales, incluidos espacios normalizables y números entre comillas. */
    @ParameterizedTest
    @ValueSource(strings = {"Empresa B", "  Empresa B  ", "123"})
    void acceptsStringName(String name) {
        String content = "{\"name\":\"" + name + "\"}";

        CompanyUpdateRequest request = mapper.readValue(content, CompanyUpdateRequest.class);

        assertThat(request.name()).isEqualTo(name.strip());
    }

    /** Rechaza cualquier campo que el DTO de entrada no declare. */
    @ParameterizedTest
    @ValueSource(strings = {"companyId", "id", "createdAt", "updatedAt", "unknown"})
    void rejectsUnknownOrReadOnlyField(String field) {
        String content = "{\"name\":\"Empresa B\",\"" + field + "\":99}";

        assertThatThrownBy(() -> mapper.readValue(content, CompanyUpdateRequest.class))
                .isInstanceOf(JacksonException.class);
    }

    /** Un número, booleano, lista u objeto no puede convertirse en el nombre de la compañía. */
    @ParameterizedTest
    @ValueSource(strings = {"123", "12.5", "true", "false", "[]", "{}"})
    void rejectsNonStringName(String jsonValue) {
        String content = "{\"name\":" + jsonValue + "}";

        assertThatThrownBy(() -> mapper.readValue(content, CompanyUpdateRequest.class))
                .isInstanceOf(JacksonException.class);
    }

    /** Exige un documento JSON completo y sin otro valor después del objeto principal. */
    @ParameterizedTest
    @ValueSource(strings = {
            "{\"name\":",
            "{\"name\":\"Empresa B\"} {}",
            "{\"name\":\"Empresa B\"} true"
    })
    void rejectsMalformedJsonOrTrailingContent(String content) {
        assertThatThrownBy(() -> mapper.readValue(content, CompanyUpdateRequest.class))
                .isInstanceOf(JacksonException.class);
    }
}
