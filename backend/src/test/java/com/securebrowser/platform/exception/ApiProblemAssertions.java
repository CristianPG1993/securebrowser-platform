package com.securebrowser.platform.exception;

import static org.hamcrest.Matchers.aMapWithSize;
import static org.hamcrest.Matchers.emptyOrNullString;
import static org.hamcrest.Matchers.everyItem;
import static org.hamcrest.Matchers.not;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.ResultMatcher;

/** Aserciones comunes para verificar el contrato HTTP completo sin repetirlo por suite. */
public final class ApiProblemAssertions {

    private ApiProblemAssertions() {
    }

    /** Exige los campos públicos exactos y conserva las cabeceras del protocolo. */
    public static ResultMatcher problem(ApiErrorCode code, String instance) {
        return result -> {
            status().is(code.getStatus().value()).match(result);
            content().contentTypeCompatibleWith(MediaType.APPLICATION_PROBLEM_JSON).match(result);
            jsonPath("$", aMapWithSize(code == ApiErrorCode.VALIDATION_ERROR ? 7 : 6)).match(result);
            jsonPath("$.type").value("about:blank").match(result);
            jsonPath("$.title").value(code.getStatus().getReasonPhrase()).match(result);
            jsonPath("$.status").value(code.getStatus().value()).match(result);
            jsonPath("$.detail").value(code.getDetail()).match(result);
            jsonPath("$.instance").value(instance).match(result);
            jsonPath("$.code").value(code.name()).match(result);
            if (code == ApiErrorCode.VALIDATION_ERROR) {
                jsonPath("$.errors").isArray().match(result);
                jsonPath("$.errors").isNotEmpty().match(result);
                jsonPath("$.errors", everyItem(aMapWithSize(2))).match(result);
                jsonPath("$.errors[*].field", everyItem(not(emptyOrNullString()))).match(result);
                jsonPath("$.errors[*].message", everyItem(not(emptyOrNullString()))).match(result);
            }
            if (code == ApiErrorCode.AUTHENTICATION_REQUIRED) {
                header().string(HttpHeaders.WWW_AUTHENTICATE, "Bearer realm=\"sbp\"").match(result);
            } else {
                header().doesNotExist(HttpHeaders.WWW_AUTHENTICATE).match(result);
            }
            if (code == ApiErrorCode.METHOD_NOT_ALLOWED) {
                header().exists(HttpHeaders.ALLOW).match(result);
            }
            if (code == ApiErrorCode.SERVICE_UNAVAILABLE) {
                header().doesNotExist(HttpHeaders.RETRY_AFTER).match(result);
            }
        };
    }
}
