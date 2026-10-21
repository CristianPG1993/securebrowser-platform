package com.securebrowser.platform.exception;

import static com.securebrowser.platform.exception.ApiProblemAssertions.problem;
import static org.hamcrest.Matchers.containsString;
import static org.hamcrest.Matchers.not;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.header;

import java.util.Map;

import com.securebrowser.platform.company.CompanyNotFoundException;

import jakarta.validation.constraints.NotBlank;

import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.boot.test.context.TestComponent;
import org.springframework.dao.CannotAcquireLockException;
import org.springframework.dao.DataAccessResourceFailureException;
import org.springframework.dao.DataIntegrityViolationException;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;
import org.springframework.transaction.CannotCreateTransactionException;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

/** Aísla la traducción de MVC; las suites HTTP de seguridad mantienen sus filtros reales. */
class ApiExceptionHandlerTest {

    private MockMvc mvc;

    @BeforeEach
    void setUp() {
        mvc = MockMvcBuilders.standaloneSetup(new ProbeController())
                .setControllerAdvice(new ApiExceptionHandler(new ApiProblemDetailFactory()))
                .build();
    }

    @Test
    void preserves405AndAllow() throws Exception {
        mvc.perform(post("/probe/document"))
                .andExpect(problem(ApiErrorCode.METHOD_NOT_ALLOWED, "/probe/document"))
                .andExpect(header().string(HttpHeaders.ALLOW, containsString("GET")));
    }

    @Test
    void preserves406ForUnsupportedResponseFormat() throws Exception {
        mvc.perform(get("/probe/document").accept(MediaType.APPLICATION_XML))
                .andExpect(problem(ApiErrorCode.NOT_ACCEPTABLE, "/probe/document"));
    }

    @Test
    void preserves415ForUnsupportedBodyFormat() throws Exception {
        mvc.perform(patch("/probe/document").contentType(MediaType.TEXT_PLAIN).content("private-test-value"))
                .andExpect(problem(ApiErrorCode.UNSUPPORTED_MEDIA_TYPE, "/probe/document"));
    }

    @ParameterizedTest
    @ValueSource(strings = {"missing", "wrong-type"})
    void sanitizesInvalidParameters(String scenario) throws Exception {
        var request = get("/probe/number");
        if (scenario.equals("wrong-type")) {
            request.param("number", "private-test-value");
        }
        mvc.perform(request)
                .andExpect(problem(ApiErrorCode.VALIDATION_ERROR, "/probe/number"))
                .andExpect(content().string(not(containsString("private-test-value"))));
    }

    @ParameterizedTest
    @CsvSource({
            "lock, SERVICE_UNAVAILABLE", "resource, SERVICE_UNAVAILABLE", "transaction, SERVICE_UNAVAILABLE",
            "argument, INTERNAL_ERROR", "integrity, INTERNAL_ERROR", "null, INTERNAL_ERROR",
            "access, ACCESS_DENIED", "authentication, AUTHENTICATION_REQUIRED", "missing, RESOURCE_NOT_FOUND"
    })
    void classifiesFailuresWithoutPublishingInternalDetails(String scenario, ApiErrorCode expected) throws Exception {
        mvc.perform(get("/probe/failure").param("scenario", scenario).param("token", "private-test-value"))
                .andExpect(problem(expected, "/probe/failure"))
                .andExpect(content().string(not(containsString("private-test-value"))))
                .andExpect(content().string(not(containsString("SELECT"))));
    }

    @Test
    void doesNotClassifyInvalidReturnValueAsClientError() throws Exception {
        mvc.perform(get("/probe/invalid-return"))
                .andExpect(problem(ApiErrorCode.INTERNAL_ERROR, "/probe/invalid-return"));
    }

    /** Endpoints exclusivos del MVC aislado; no se registran en la aplicación. */
    @TestComponent
    @RestController
    static class ProbeController {

        @GetMapping(value = "/probe/document", produces = MediaType.APPLICATION_JSON_VALUE)
        public Map<String, String> document() {
            return Map.of("name", "test");
        }

        @PatchMapping(value = "/probe/document", consumes = MediaType.APPLICATION_JSON_VALUE)
        public Map<String, String> update(@RequestBody Map<String, String> body) {
            return body;
        }

        @GetMapping("/probe/number")
        public int number(@RequestParam int number) {
            return number;
        }

        @GetMapping("/probe/invalid-return")
        @NotBlank
        public String invalidReturn() {
            return "";
        }

        @GetMapping("/probe/failure")
        public void failure(@RequestParam String scenario) {
            String detail = "private-test-value SELECT password_hash FROM users";
            throw switch (scenario) {
                case "lock" -> new CannotAcquireLockException(detail);
                case "resource" -> new DataAccessResourceFailureException(detail);
                case "transaction" -> new CannotCreateTransactionException(detail);
                case "argument" -> new IllegalArgumentException(detail);
                case "integrity" -> new DataIntegrityViolationException(detail);
                case "null" -> new NullPointerException(detail);
                case "access" -> new AccessDeniedException(detail);
                case "authentication" -> new BadCredentialsException(detail);
                case "missing" -> new CompanyNotFoundException();
                default -> new IllegalStateException(detail);
            };
        }
    }
}
