package com.securebrowser.platform.demo;

import static org.assertj.core.api.Assertions.assertThat;
import java.util.Map;
import com.securebrowser.platform.auth.AuthService;
import com.securebrowser.platform.auth.dto.LoginRequest;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;
import org.junit.jupiter.api.Test;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.DefaultApplicationArguments;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.context.annotation.Import;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.test.context.ActiveProfiles;

/** Valida la fixture completa y su conservación tras repetir el arranque demo. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE, properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-demo-test", "sbp.security.jwt.audience=sbp-demo-api",
        "sbp.demo.admin-password=Admin demo 2026!", "sbp.demo.user-password=User demo 2026!"
})
@ActiveProfiles("demo")
@Import(PostgresRepositoryTestConfiguration.class)
class DemoDataPersistenceTest {
    @Autowired private DemoDataInitializer initializer;
    @Autowired private JdbcTemplate jdbc;
    @Autowired private UserRepository users;
    @Autowired private PasswordEncoder encoder;
    @Autowired private AuthService auth;

    @Test
    void createsCompleteUsableDemoAndPreservesEditsOnRestart() {
        User admin = users.findByEmail(DemoDataInitializer.ADMIN_EMAIL).orElseThrow();
        User user = users.findByEmail(DemoDataInitializer.USER_EMAIL).orElseThrow();
        // Cada cuenta tiene el rol requerido por el recorrido de demostración.
        assertThat(admin.getRole()).isEqualTo(UserRole.ADMIN);
        // El dispositivo del demo pertenece a un USER.
        assertThat(user.getRole()).isEqualTo(UserRole.USER);
        // Ambas identidades pertenecen a la misma compañía demo.
        assertThat(user.getCompany().getId()).isEqualTo(admin.getCompany().getId());
        // El ADMIN puede iniciar sesión con la contraseña inicial recibida de configuración.
        assertThat(auth.login(new LoginRequest(admin.getEmail(), "Admin demo 2026!")).accessToken()).isNotBlank();
        // El USER también recibe tokens válidos para el recorrido.
        assertThat(auth.login(new LoginRequest(user.getEmail(), "User demo 2026!")).accessToken()).isNotBlank();

        Map<String, Object> device = jdbc.queryForMap("""
                select d.active, d.user_id, d.company_id, d.policy_id, d.license_id,
                       e.used_at, e.revoked_at, e.user_id as enrollment_user,
                       e.policy_id as enrollment_policy, e.license_id as enrollment_license
                from devices d join enrollment_tokens e on e.id = d.enrollment_token_id
                where d.device_identifier = ?
                """, DemoDataInitializer.DEVICE_IDENTIFIER);
        // El enrollment ya está consumido y el dispositivo está activo.
        assertThat(device.get("used_at")).isNotNull();
        // No se confunde consumo con revocación del enrollment.
        assertThat(device.get("revoked_at")).isNull();
        // La instalación empieza activa.
        assertThat(device.get("active")).isEqualTo(true);
        // El propietario del dispositivo es el USER del demo.
        assertThat(device.get("user_id")).isEqualTo(user.getId());
        // La política coincide con la referencia histórica del enrollment.
        assertThat(device.get("policy_id")).isEqualTo(device.get("enrollment_policy"));
        // La licencia conserva también su referencia de origen.
        assertThat(device.get("license_id")).isEqualTo(device.get("enrollment_license"));
        // La política contiene la regla de dominio acordada para la demostración.
        assertThat(jdbc.queryForObject("select domain from url_rules", String.class)).isEqualTo("blocked.example");
        // Incluye una regla de descarga utilizable desde el bloque de políticas.
        assertThat(jdbc.queryForObject("select extension from download_rules", String.class)).isEqualTo("exe");

        admin.changeName("Admin editado");
        admin.changePasswordHash(encoder.encode("Updated demo 2026!"));
        users.saveAndFlush(admin);
        jdbc.update("update policies set name = ?", "Política editada");
        initializer.run(new DefaultApplicationArguments());

        // El arranque repetido no duplica usuarios.
        assertThat(jdbc.queryForObject("select count(*) from users", Long.class)).isEqualTo(2L);
        // Tampoco duplica compañías.
        assertThat(jdbc.queryForObject("select count(*) from companies", Long.class)).isEqualTo(1L);
        // El dispositivo sigue siendo único.
        assertThat(jdbc.queryForObject("select count(*) from devices", Long.class)).isEqualTo(1L);
        // Las modificaciones del perfil permanecen intactas.
        assertThat(users.findByEmail(admin.getEmail()).orElseThrow().getName()).isEqualTo("Admin editado");
        // No se restablece la contraseña inicial al reiniciar.
        assertThat(encoder.matches("Updated demo 2026!",
                users.findByEmail(admin.getEmail()).orElseThrow().getPasswordHash())).isTrue();
        // Las ediciones de la política también sobreviven al reinicio.
        assertThat(jdbc.queryForObject("select name from policies", String.class)).isEqualTo("Política editada");
    }
}
