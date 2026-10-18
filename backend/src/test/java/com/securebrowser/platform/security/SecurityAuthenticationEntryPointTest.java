package com.securebrowser.platform.security;

import static org.assertj.core.api.Assertions.assertThat;

import org.junit.jupiter.api.Test;
import org.springframework.http.HttpHeaders;
import org.springframework.mock.web.MockHttpServletRequest;
import org.springframework.mock.web.MockHttpServletResponse;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.authentication.InsufficientAuthenticationException;
import org.springframework.security.oauth2.core.OAuth2AuthenticationException;
import org.springframework.security.oauth2.core.OAuth2Error;

/** Comprueba estados y desafíos Bearer sin divulgar los detalles de las excepciones. */
class SecurityAuthenticationEntryPointTest {

    private final SecurityAuthenticationEntryPoint entryPoint = new SecurityAuthenticationEntryPoint();

    /** Solicita autenticación Bearer cuando no hay una identidad autenticada. */
    @Test
    void challengesMissingAuthentication() throws Exception {
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(new MockHttpServletRequest(), response,
                new InsufficientAuthenticationException("Internal detail"));

        assertThat(response.getStatus()).isEqualTo(401);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE)).isEqualTo("Bearer");
        assertThat(response.getContentAsString()).isEmpty();
    }

    /** La respuesta no contiene el mensaje interno ni el token rechazado. */
    @Test
    void sanitizesInvalidCredentials() throws Exception {
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(new MockHttpServletRequest(), response,
                new BadCredentialsException("Secret token or database detail"));

        assertThat(response.getStatus()).isEqualTo(401);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE))
                .isEqualTo("Bearer error=\"invalid_token\"");
        assertThat(response.getContentAsString()).isEmpty();
    }

    /** Un fallo de disponibilidad devuelve 503 sin invitar a renovar credenciales. */
    @Test
    void distinguishesTemporaryUnavailability() throws Exception {
        MockHttpServletResponse response = new MockHttpServletResponse();

        entryPoint.commence(new MockHttpServletRequest(), response,
                new OAuth2AuthenticationException(new OAuth2Error("temporarily_unavailable"),
                        "Internal database detail"));

        assertThat(response.getStatus()).isEqualTo(503);
        assertThat(response.getHeader(HttpHeaders.WWW_AUTHENTICATE)).isNull();
        assertThat(response.getContentAsString()).isEmpty();
    }
}
