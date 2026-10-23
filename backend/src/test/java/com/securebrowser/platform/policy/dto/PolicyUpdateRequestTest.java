package com.securebrowser.platform.policy.dto;

import static org.assertj.core.api.Assertions.*;
import com.securebrowser.platform.policy.FilterMode;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.ValueSource;
import tools.jackson.databind.DeserializationFeature;
import tools.jackson.databind.json.JsonMapper;

/** Comprueba presencia y tipos originales sin arrancar Spring ni PostgreSQL. */
class PolicyUpdateRequestTest {
    private final JsonMapper mapper = JsonMapper.builder()
            .enable(DeserializationFeature.FAIL_ON_TRAILING_TOKENS).build();

    @Test
    void distinguishesOmissionFalseAndEmptyLists() {
        PolicyUpdateRequest request = mapper.readValue(
                "{\"urlFilteringEnabled\":false,\"urlRules\":[],\"downloadControlMode\":\"ALLOWLIST\"}",
                PolicyUpdateRequest.class);
        // Un campo ausente no ordena cambiar el nombre.
        assertThat(request.name()).isNull();
        // false es un valor presente y se conserva como tal.
        assertThat(request.urlFilteringEnabled()).isFalse();
        // Una lista vacía expresa que deben retirarse todas sus reglas.
        assertThat(request.urlRules()).isEmpty();
        // Omitir la otra lista permite conservar sus reglas.
        assertThat(request.downloadRules()).isNull();
        // El modo se lee con el enum exacto del contrato.
        assertThat(request.downloadControlMode()).isEqualTo(FilterMode.ALLOWLIST);
    }

    @ParameterizedTest
    @ValueSource(strings = {
        "{}", "[]", "\"name\"", "{\"name\":null}", "{\"companyId\":1}", "{\"name\":123}",
        "{\"urlFilteringEnabled\":\"true\"}", "{\"urlFilteringEnabled\":1}",
        "{\"downloadControlEnabled\":\"false\"}", "{\"urlFilteringMode\":0}",
        "{\"urlFilteringMode\":\"allowlist\"}", "{\"downloadControlMode\":null}",
        "{\"urlRules\":\"example.com\"}", "{\"urlRules\":[null]}", "{\"urlRules\":[4]}",
        "{\"downloadRules\":[true]}", "{\"downloadRules\":null}", "{\"urlRules\":[]} {}"
    })
    void rejectsAmbiguousOrIncorrectJson(String json) {
        // El DTO rechaza campos ajenos, null y conversiones implícitas antes de llamar al servicio.
        assertThatThrownBy(() -> mapper.readValue(json, PolicyUpdateRequest.class)).isInstanceOf(RuntimeException.class);
    }
}
