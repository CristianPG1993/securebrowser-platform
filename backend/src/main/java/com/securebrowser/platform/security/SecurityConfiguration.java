package com.securebrowser.platform.security;

import jakarta.servlet.DispatcherType;

import org.springframework.boot.autoconfigure.condition.ConditionalOnWebApplication;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Import;
import org.springframework.http.HttpMethod;
import org.springframework.security.authentication.ProviderManager;
import org.springframework.security.config.annotation.method.configuration.EnableMethodSecurity;
import org.springframework.security.config.annotation.web.builders.HttpSecurity;
import org.springframework.security.config.annotation.web.configuration.EnableWebSecurity;
import org.springframework.security.config.annotation.web.configurers.AbstractHttpConfigurer;
import org.springframework.security.config.http.SessionCreationPolicy;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.security.oauth2.server.resource.authentication.JwtAuthenticationProvider;
import org.springframework.security.oauth2.server.resource.web.authentication.BearerTokenAuthenticationFilter;
import org.springframework.security.web.SecurityFilterChain;
import org.springframework.security.web.authentication.AnonymousAuthenticationFilter;

/** Protege la API mediante Bearer JWT, permisos explícitos y ausencia de sesión HTTP. */
@Configuration(proxyBeanMethods = false)
@ConditionalOnWebApplication(type = ConditionalOnWebApplication.Type.SERVLET)
@EnableWebSecurity
@EnableMethodSecurity
@Import(SecurityAccessDeniedHandler.class)
public class SecurityConfiguration {

    /** Conecta la validación del JWT con la identidad actual y los permisos HTTP. */
    @Bean
    public SecurityFilterChain securityFilterChain(HttpSecurity http,
            JwtDecoder decoder,
            AuthenticatedUserJwtConverter converter,
            SecurityAuthenticationEntryPoint entryPoint,
            SecurityAccessDeniedHandler accessDeniedHandler) throws Exception {
        JwtAuthenticationProvider provider = new JwtAuthenticationProvider(decoder);
        provider.setJwtAuthenticationConverter(converter);

        // Usa el filtro de Spring sin habilitar endpoints adicionales de descubrimiento.
        BearerTokenAuthenticationFilter bearerFilter =
                new BearerTokenAuthenticationFilter(new ProviderManager(provider));
        bearerFilter.setAuthenticationEntryPoint(entryPoint);

        http
                // La API usa Authorization: Bearer, sin autenticación mediante cookies.
                .csrf(AbstractHttpConfigurer::disable)
                .httpBasic(AbstractHttpConfigurer::disable)
                .formLogin(AbstractHttpConfigurer::disable)
                .logout(AbstractHttpConfigurer::disable)
                .requestCache(AbstractHttpConfigurer::disable)
                .sessionManagement(session ->
                        session.sessionCreationPolicy(SessionCreationPolicy.STATELESS))
                .authorizeHttpRequests(authorize -> authorize
                        // Permite presentar errores internos conservando su estado HTTP original.
                        .dispatcherTypeMatchers(DispatcherType.ERROR).permitAll()
                        .requestMatchers(HttpMethod.POST, "/api/v1/auth/login", "/api/v1/auth/refresh",
                                "/api/v1/auth/logout").permitAll()
                        .requestMatchers(HttpMethod.GET, "/api/v1/users/me").hasAnyRole("ADMIN", "USER")
                        .requestMatchers(HttpMethod.GET, "/api/v1/companies/me").hasRole("ADMIN")
                        .requestMatchers(HttpMethod.PATCH, "/api/v1/companies/me").hasRole("ADMIN")
                        .requestMatchers(HttpMethod.GET, "/api/v1/policies", "/api/v1/policies/{id}").hasRole("ADMIN")
                        .requestMatchers(HttpMethod.PATCH, "/api/v1/policies/{id}").hasRole("ADMIN")
                        .requestMatchers(HttpMethod.GET, "/api/v1/devices", "/api/v1/devices/{id}/policy")
                                .hasAnyRole("ADMIN", "USER")
                        .anyRequest().denyAll())
                .exceptionHandling(exceptions -> exceptions
                        .authenticationEntryPoint(entryPoint)
                        .accessDeniedHandler(accessDeniedHandler))
                .addFilterBefore(bearerFilter, AnonymousAuthenticationFilter.class);

        return http.build();
    }
}
