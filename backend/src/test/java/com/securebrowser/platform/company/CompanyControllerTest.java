package com.securebrowser.platform.company;

import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;
import static org.mockito.Mockito.verifyNoMoreInteractions;
import static org.mockito.Mockito.when;
import static org.springframework.security.test.web.servlet.request.SecurityMockMvcRequestPostProcessors.authentication;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.patch;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.content;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

import java.time.Instant;
import java.util.stream.Stream;

import com.securebrowser.platform.config.JsonConfiguration;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.security.AuthenticatedUserAuthenticationToken;
import com.securebrowser.platform.security.AuthenticatedUserJwtConverter;
import com.securebrowser.platform.security.AuthenticatedUserResolver;
import com.securebrowser.platform.security.SecurityAuthenticationEntryPoint;
import com.securebrowser.platform.security.SecurityConfiguration;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.MethodSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.webmvc.test.autoconfigure.WebMvcTest;
import org.springframework.context.annotation.Import;
import org.springframework.http.MediaType;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.test.context.bean.override.mockito.MockitoBean;
import org.springframework.test.json.JsonCompareMode;
import org.springframework.test.web.servlet.MockMvc;

/** Comprueba el contrato HTTP con filtros reales e identidades y servicios controlados. */
@WebMvcTest(CompanyController.class)
@Import({JsonConfiguration.class, SecurityConfiguration.class,
        AuthenticatedUserJwtConverter.class, SecurityAuthenticationEntryPoint.class})
class CompanyControllerTest {

    // Ruta pública de consulta y actualización de la compañía propia.
    private static final String COMPANY_PATH = "/api/v1/companies/me";

    // Envía peticiones de prueba al Controller con la cadena de seguridad activada.
    @Autowired
    private MockMvc mvc;

    // Permite comprobar la delegación del Controller sin conectar con PostgreSQL.
    @MockitoBean
    private CompanyService companyService;

    // Completa la configuración de seguridad; estos casos preparan directamente la identidad.
    @MockitoBean
    private JwtDecoder jwtDecoder;

    @MockitoBean
    private AuthenticatedUserResolver userResolver;

    /** Un ADMIN recibe exactamente los cuatro campos públicos y las fechas UTC. */
    @Test
    void returnsOwnCompanyWithExactResponseFields() throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);
        Company company = mock(Company.class);
        when(company.getId()).thenReturn(7L);
        when(company.getName()).thenReturn("Empresa A");
        when(company.getCreatedAt()).thenReturn(Instant.parse("2026-10-18T08:00:00Z"));
        when(company.getUpdatedAt()).thenReturn(Instant.parse("2026-10-18T09:00:00Z"));
        when(companyService.getOwnCompany(admin)).thenReturn(company);

        mvc.perform(get(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(content().json("""
                        {
                            "id": 7,
                            "name": "Empresa A",
                            "createdAt": "2026-10-18T08:00:00Z",
                            "updatedAt": "2026-10-18T09:00:00Z"
                        }
                        """, JsonCompareMode.STRICT));

        verify(companyService).getOwnCompany(admin);
        verifyNoMoreInteractions(companyService);
        verifyNoInteractions(jwtDecoder, userResolver);
    }

    /** Un ADMIN envía un nombre con espacios y el servicio recibe el nombre normalizado. */
    @Test
    void updatesOwnCompanyWithNormalizedName() throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);
        Company company = mock(Company.class);
        when(company.getId()).thenReturn(7L);
        when(company.getName()).thenReturn("Empresa renovada");
        when(company.getCreatedAt()).thenReturn(Instant.parse("2026-10-18T08:00:00Z"));
        when(company.getUpdatedAt()).thenReturn(Instant.parse("2026-10-18T10:00:00Z"));
        when(companyService.changeOwnCompanyName(admin, "Empresa renovada")).thenReturn(company);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "  Empresa renovada  "}
                                """))
                .andExpect(status().isOk())
                .andExpect(content().contentTypeCompatibleWith(MediaType.APPLICATION_JSON))
                .andExpect(content().json("""
                        {
                            "id": 7,
                            "name": "Empresa renovada",
                            "createdAt": "2026-10-18T08:00:00Z",
                            "updatedAt": "2026-10-18T10:00:00Z"
                        }
                        """, JsonCompareMode.STRICT));

        verify(companyService).changeOwnCompanyName(admin, "Empresa renovada");
        verifyNoMoreInteractions(companyService);
        verifyNoInteractions(jwtDecoder, userResolver);
    }

    /** La validación rechaza un nombre en blanco antes de llamar al servicio. */
    @Test
    void rejectsBlankNameWithoutCallingService() throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "   "}
                                """))
                .andExpect(status().isBadRequest());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Rechaza nombres ausentes, nulos, vacíos o de más de 150 caracteres. */
    @ParameterizedTest
    @MethodSource("invalidNameBodies")
    void rejectsInvalidNameWithoutCallingService(String body) throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content(body))
                .andExpect(status().isBadRequest());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Rechaza cuerpos que no se pueden convertir al DTO respetando el contrato JSON. */
    @ParameterizedTest
    @MethodSource("invalidJsonBodies")
    void rejectsInvalidJsonWithoutCallingService(String body) throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content(body))
                .andExpect(status().isBadRequest());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Sin una identidad autenticada no se permite consultar la compañía. */
    @Test
    void rejectsGetWithoutAuthentication() throws Exception {
        mvc.perform(get(COMPANY_PATH)
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isUnauthorized());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Sin una identidad autenticada tampoco se permite modificar el nombre. */
    @Test
    void rejectsPatchWithoutAuthentication() throws Exception {
        mvc.perform(patch(COMPANY_PATH)
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "Empresa renovada"}
                                """))
                .andExpect(status().isUnauthorized());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Un usuario autenticado con rol USER no puede consultar la compañía. */
    @Test
    void rejectsGetForUserRole() throws Exception {
        AuthenticatedUser user = new AuthenticatedUser(26L, 7L, UserRole.USER);

        mvc.perform(get(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(user)))
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isForbidden());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Un usuario autenticado con rol USER no puede modificar el nombre. */
    @Test
    void rejectsPatchForUserRole() throws Exception {
        AuthenticatedUser user = new AuthenticatedUser(26L, 7L, UserRole.USER);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(user)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "Empresa renovada"}
                                """))
                .andExpect(status().isForbidden());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** La excepción del servicio se traduce a 404 al consultar la compañía. */
    @Test
    void returnsNotFoundWhenOwnCompanyCannotBeRead() throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);
        when(companyService.getOwnCompany(admin)).thenThrow(new CompanyNotFoundException());

        mvc.perform(get(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .accept(MediaType.APPLICATION_JSON))
                .andExpect(status().isNotFound());

        verify(companyService).getOwnCompany(admin);
        verifyNoMoreInteractions(companyService);
        verifyNoInteractions(jwtDecoder, userResolver);
    }

    /** La excepción del servicio se traduce a 404 al modificar la compañía. */
    @Test
    void returnsNotFoundWhenOwnCompanyCannotBeUpdated() throws Exception {
        AuthenticatedUser admin = new AuthenticatedUser(25L, 7L, UserRole.ADMIN);
        when(companyService.changeOwnCompanyName(admin, "Empresa renovada"))
                .thenThrow(new CompanyNotFoundException());

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(admin)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content("""
                                {"name": "Empresa renovada"}
                                """))
                .andExpect(status().isNotFound());

        verify(companyService).changeOwnCompanyName(admin, "Empresa renovada");
        verifyNoMoreInteractions(companyService);
        verifyNoInteractions(jwtDecoder, userResolver);
    }

    /** La ausencia de autenticación se rechaza antes de leer o validar un cuerpo incorrecto. */
    @ParameterizedTest
    @ValueSource(strings = {"{}", "{\"name\": \"   \"}", "{\"name\":"})
    void rejectsUnauthenticatedPatchBeforeValidatingBody(String body) throws Exception {
        mvc.perform(patch(COMPANY_PATH)
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content(body))
                .andExpect(status().isUnauthorized());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** El rol insuficiente se rechaza antes de leer o validar un cuerpo incorrecto. */
    @ParameterizedTest
    @ValueSource(strings = {"{}", "{\"name\": \"   \"}", "{\"name\":"})
    void rejectsUserPatchBeforeValidatingBody(String body) throws Exception {
        AuthenticatedUser user = new AuthenticatedUser(26L, 7L, UserRole.USER);

        mvc.perform(patch(COMPANY_PATH)
                        .with(authentication(new AuthenticatedUserAuthenticationToken(user)))
                        .contentType(MediaType.APPLICATION_JSON)
                        .accept(MediaType.APPLICATION_JSON)
                        .content(body))
                .andExpect(status().isForbidden());

        verifyNoInteractions(companyService, jwtDecoder, userResolver);
    }

    /** Proporciona un cuerpo JSON distinto para cada ejecución de la prueba. */
    private static Stream<String> invalidNameBodies() {
        return Stream.of(
                "{}",
                "{\"name\": null}",
                "{\"name\": \"\"}",
                "{\"name\": \"%s\"}".formatted("a".repeat(151)));
    }

    /** Incluye cuerpos vacíos, tipos incorrectos, campos extra y sintaxis inválida. */
    private static Stream<String> invalidJsonBodies() {
        return Stream.of(
                "",
                "null",
                "[]",
                "{\"name\": 123}",
                "{\"name\": 12.5}",
                "{\"name\": true}",
                "{\"name\": []}",
                "{\"name\": {}}",
                "{\"name\": \"Empresa A\", \"extra\": \"valor\"}",
                "{\"name\": \"Empresa A\", \"id\": 99}",
                "{\"name\":",
                "{\"name\": \"Empresa A\"} {}");
    }
}
