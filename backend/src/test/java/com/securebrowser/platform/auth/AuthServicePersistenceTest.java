package com.securebrowser.platform.auth;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;

import java.time.Clock;
import java.time.Instant;
import java.time.ZoneOffset;
import java.util.List;
import java.util.Locale;
import java.util.UUID;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import com.securebrowser.platform.auth.dto.LoginRequest;
import com.securebrowser.platform.auth.dto.TokenResponse;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.company.CompanyRepository;
import com.securebrowser.platform.testsupport.PostgresRepositoryTestConfiguration;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;
import com.securebrowser.platform.user.UserRole;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.EnumSource;
import org.junit.jupiter.params.provider.ValueSource;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.boot.test.context.SpringBootTest;
import org.springframework.boot.test.context.TestConfiguration;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Import;
import org.springframework.context.annotation.Primary;
import org.springframework.dao.PessimisticLockingFailureException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.security.oauth2.jwt.JwtDecoder;
import org.springframework.test.context.bean.override.mockito.MockitoSpyBean;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;

/** Comprueba sesiones confirmadas con PostgreSQL, BCrypt y firma y verificación JWT reales. */
@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.NONE, properties = {
        "sbp.security.jwt.secret-base64=AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        "sbp.security.jwt.issuer=sbp-auth-test",
        "sbp.security.jwt.audience=sbp-auth-api",
        "sbp.security.jwt.max-lifetime-seconds=900",
        "sbp.auth.refresh-token-lifetime-seconds=604800"
})
@Import({ PostgresRepositoryTestConfiguration.class, AuthServicePersistenceTest.FixedClockConfiguration.class })
class AuthServicePersistenceTest {

    private static final Instant NOW = Instant.parse("2026-10-23T08:00:00Z");
    private static final String PASSWORD = " Clave demo 2026! ";

    @Autowired
    private AuthService authService;
    @Autowired
    private CompanyRepository companyRepository;
    @Autowired
    private UserRepository userRepository;
    @Autowired
    private RefreshTokenRepository refreshTokenRepository;
    @Autowired
    private PasswordEncoder passwordEncoder;
    @Autowired
    private JwtDecoder jwtDecoder;
    @Autowired
    private RefreshTokenHasher refreshTokenHasher;
    @Autowired
    private JdbcTemplate jdbc;
    @Autowired
    private PlatformTransactionManager transactionManager;

    // Conservan el comportamiento real salvo en las pruebas que simulan una condición concreta.
    @MockitoSpyBean
    private AccessTokenService accessTokenService;
    @MockitoSpyBean
    private AuthProperties authProperties;
    @MockitoSpyBean(name = "authServiceTestClock")
    private Clock clock;

    // Limita la limpieza a los datos creados por esta prueba.
    private Long companyId;

    /** Al borrar sus usuarios, PostgreSQL elimina también sus refresh tokens mediante la FK con cascada. */
    @AfterEach
    void removeFixtures() {
        if (companyId != null) {
            jdbc.update("delete from users where company_id = ?", companyId);
            jdbc.update("delete from companies where id = ?", companyId);
        }
    }

    /** Un login correcto devuelve tokens verificables y confirma el hash de la sesión en PostgreSQL. */
    @ParameterizedTest
    @EnumSource(UserRole.class)
    void loginCreatesCommittedSessionAndReturnsVerifiableTokens(UserRole role) {
        User user = persistUser(role);

        // El DTO normaliza el email; los espacios y mayúsculas de la contraseña se conservan.
        TokenResponse response = authService.login(new LoginRequest(
                "  " + user.getEmail().toUpperCase(Locale.ROOT) + "  ", PASSWORD));
        Jwt accessToken = jwtDecoder.decode(response.accessToken());

        // El JWT firmado identifica al usuario que acabamos de autenticar.
        assertThat(accessToken.getSubject()).isEqualTo(user.getId().toString());
        // La respuesta indica que el JWT se utilizará como Bearer.
        assertThat(response.tokenType()).isEqualTo("Bearer");
        // La sesión permite emitir los 900 segundos completos del JWT configurado.
        assertThat(response.expiresIn()).isEqualTo(900L);
        // La caducidad firmada coincide con la duración entregada al cliente.
        assertThat(accessToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(900));

        // Esta lectura se realiza después del retorno del servicio, en otra operación de persistencia.
        List<RefreshToken> storedTokens = refreshTokenRepository
                .findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId());

        // El grupo y su primer refresh token ya están confirmados en la base de datos.
        assertThat(storedTokens).hasSize(1);
        RefreshToken storedToken = storedTokens.getFirst();
        // Se guarda la huella SHA-256 del secreto devuelto, en lugar del secreto original.
        assertThat(storedToken.getTokenHash()).isEqualTo(refreshTokenHasher.hash(response.refreshToken()));
        // El login asigna un identificador de grupo generado por el backend.
        assertThat(storedToken.getFamilyId()).isNotNull();
        // La creación utiliza el reloj fijo del backend.
        assertThat(storedToken.getCreatedAt()).isEqualTo(NOW);
        // El grupo conserva la duración absoluta de siete días configurada para esta prueba.
        assertThat(storedToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(604800));
        // El refresh recién creado está vigente y todavía no ha sido consumido ni revocado.
        assertThat(storedToken.isUsableAt(NOW)).isTrue();
    }

    /** Un email desconocido y una contraseña incorrecta tienen el mismo rechazo, sin crear sesiones. */
    @ParameterizedTest
    @ValueSource(booleans = { false, true })
    void rejectsInvalidCredentialsWithoutCreatingSession(boolean unknownEmail) {
        User user = persistUser(UserRole.ADMIN);

        // false: email correcto y contraseña incorrecta; true: email desconocido y contraseña original.
        String attemptedEmail = unknownEmail ? UUID.randomUUID() + "@example.com" : user.getEmail();
        String attemptedPassword = unknownEmail ? PASSWORD : "Otra clave incorrecta";
        LoginRequest request = new LoginRequest(attemptedEmail, attemptedPassword);

        // Ambos casos producen la misma excepción y mensaje, sin revelar si existe el usuario.
        assertThatThrownBy(() -> authService.login(request))
                .isInstanceOf(InvalidCredentialsException.class)
                .hasMessage("Invalid credentials");
        // Tras el rechazo no queda ningún grupo de renovación creado para este usuario.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                .isEmpty();
    }

    /** El servicio no acepta variantes que recorten espacios o cambien las mayúsculas de la contraseña. */
    @ParameterizedTest
    @ValueSource(strings = { "Clave demo 2026!", " clave demo 2026! " })
    void rejectsModifiedPasswordWithoutCreatingSession(String modifiedPassword) {
        User user = persistUser(UserRole.USER);

        // Una contraseña parecida sigue siendo incorrecta: se compara el valor exacto recibido.
        assertThatThrownBy(() -> authService.login(new LoginRequest(user.getEmail(), modifiedPassword)))
                .isInstanceOf(InvalidCredentialsException.class)
                .hasMessage("Invalid credentials");
        // La comparación fallida no crea ningún grupo de renovación.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                .isEmpty();
    }

    /** Cada login abre un grupo independiente sin revocar las demás sesiones del usuario. */
    @Test
    void repeatedLoginsCreateIndependentUsableFamilies() {
        User user = persistUser(UserRole.USER);
        LoginRequest request = new LoginRequest(user.getEmail(), PASSWORD);

        TokenResponse first = authService.login(request);
        TokenResponse second = authService.login(request);
        List<RefreshToken> storedTokens = refreshTokenRepository
                .findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId());

        // Cada login entrega un secreto nuevo para su propia sesión.
        assertThat(second.refreshToken()).isNotEqualTo(first.refreshToken());
        // Las dos sesiones están confirmadas en PostgreSQL.
        assertThat(storedTokens).hasSize(2);
        // Los registros pertenecen a grupos distintos, aunque sean del mismo usuario.
        assertThat(storedTokens).extracting(RefreshToken::getFamilyId).doesNotHaveDuplicates();
        // Cada registro contiene el hash del secreto entregado en su respuesta.
        assertThat(storedTokens).extracting(RefreshToken::getTokenHash).containsExactlyInAnyOrder(
                refreshTokenHasher.hash(first.refreshToken()), refreshTokenHasher.hash(second.refreshToken()));
        // Un segundo login no consume ni revoca el refresh token de la primera sesión.
        assertThat(storedTokens).allMatch(token -> token.isUsableAt(NOW));
    }

    /** Una sesión configurada a tres minutos limita también la duración del JWT a tres minutos. */
    @Test
    void configuredShortSessionCapsJwtAndResponseLifetime() {
        User user = persistUser(UserRole.ADMIN);
        doReturn(180L).when(authProperties).refreshTokenLifetimeSeconds();

        TokenResponse response = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        Jwt accessToken = jwtDecoder.decode(response.accessToken());
        List<RefreshToken> storedTokens = refreshTokenRepository
                .findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId());

        // Se confirma una sola sesión con la duración reducida de este caso.
        assertThat(storedTokens).hasSize(1);
        // El grupo caduca a los 180 segundos configurados, en lugar de a los siete días.
        assertThat(storedTokens.getFirst().getExpiresAt()).isEqualTo(NOW.plusSeconds(180));
        // El JWT firmado respeta el límite de ese grupo.
        assertThat(accessToken.getExpiresAt()).isEqualTo(NOW.plusSeconds(180));
        // El cliente recibe la duración efectiva de 180 segundos.
        assertThat(response.expiresIn()).isEqualTo(180L);
    }

    /** Si falla la emisión del JWT, se revierte el refresh insertado y se libera el bloqueo de User. */
    @Test
    void rollsBackSessionAndAllowsRetryWhenJwtIssuanceFails() {
        User user = persistUser(UserRole.ADMIN);
        LoginRequest request = new LoginRequest(user.getEmail(), PASSWORD);
        IllegalStateException signingFailure = new IllegalStateException("Simulated JWT issuance failure");
        // Falla solo la primera emisión; el intento posterior vuelve a ejecutar el firmador real.
        doThrow(signingFailure).doCallRealMethod().when(accessTokenService)
                .issue(eq(user.getId()), any(Instant.class));

        // El fallo de emisión se propaga y provoca el rollback de la transacción del login.
        assertThatThrownBy(() -> authService.login(request)).isSameAs(signingFailure);
        // Aunque el INSERT del refresh se ejecutó, ningún registro queda confirmado tras el fallo.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                .isEmpty();

        TokenResponse retry = authService.login(request);

        // Otro login puede adquirir el bloqueo liberado y emitir un JWT real para el mismo usuario.
        assertThat(jwtDecoder.decode(retry.accessToken()).getSubject()).isEqualTo(user.getId().toString());
        // Solo queda la sesión del intento que terminó correctamente.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                .hasSize(1);
    }

    /** Un cambio concurrente de contraseña impide crear una sesión con credenciales obsoletas. */
    @Test
    void rejectsBusyUserAndChecksPasswordAfterConcurrentChange() throws Exception {
        User user = persistUser(UserRole.ADMIN);
        String updatedPassword = " Nueva clave demo 2026! ";
        String updatedHash = passwordEncoder.encode(updatedPassword);
        CountDownLatch locked = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        TransactionTemplate transaction = new TransactionTemplate(transactionManager);
        transaction.setTimeout(10);

        try (var executor = Executors.newSingleThreadExecutor()) {
            var change = executor.submit(() -> transaction.executeWithoutResult(status -> {
                User lockedUser = userRepository.findByIdAndCompanyIdForUpdate(user.getId(), companyId)
                        .orElseThrow();
                lockedUser.changePasswordHash(updatedHash);
                userRepository.saveAndFlush(lockedUser);
                locked.countDown();
                awaitRelease(release);
            }));

            try {
                // El cambio mantiene la fila bloqueada antes de intentar el login desde otra conexión.
                assertThat(locked.await(10, TimeUnit.SECONDS)).isTrue();
                // El login respeta el bloqueo NOWAIT, sin crear una sesión mientras cambian las credenciales.
                assertThatThrownBy(() -> authService.login(new LoginRequest(user.getEmail(), PASSWORD)))
                        .isInstanceOf(PessimisticLockingFailureException.class);
                // El conflicto de bloqueo no deja un refresh token persistido.
                assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                        .isEmpty();
            } finally {
                release.countDown();
                change.get(10, TimeUnit.SECONDS);
            }
        }

        // Después del commit se comprueba la contraseña actual: la anterior ya no sirve.
        assertThatThrownBy(() -> authService.login(new LoginRequest(user.getEmail(), PASSWORD)))
                .isInstanceOf(InvalidCredentialsException.class);

        TokenResponse response = authService.login(new LoginRequest(user.getEmail(), updatedPassword));

        // La nueva contraseña permite emitir un JWT verificable para el usuario.
        assertThat(jwtDecoder.decode(response.accessToken()).getSubject()).isEqualTo(user.getId().toString());
        // Solo se confirma la sesión creada con las credenciales nuevas.
        assertThat(refreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc(user.getId()))
                .hasSize(1);
    }

    @Test
    void refreshRotatesSecretAndPreservesAbsoluteFamilyExpiry() {
        User user = persistUser(UserRole.USER);
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        RefreshToken first = storedToken(user, login.refreshToken());
        doReturn(NOW.plusSeconds(86400)).when(clock).instant();

        TokenResponse renewed = authService.refresh(login.refreshToken());
        RefreshToken previous = storedToken(user, login.refreshToken());
        RefreshToken next = storedToken(user, renewed.refreshToken());

        // La rotación sustituye el secreto original.
        assertThat(renewed.refreshToken()).isNotEqualTo(login.refreshToken());
        // El consumo queda confirmado antes de devolver el nuevo par.
        assertThat(previous.getUsedAt()).isEqualTo(NOW.plusSeconds(86400));
        // La rotación pertenece al mismo grupo de login.
        assertThat(next.getFamilyId()).isEqualTo(first.getFamilyId());
        // Renovar un día después no añade otro día a la caducidad original.
        assertThat(next.getExpiresAt()).isEqualTo(first.getExpiresAt());
        // El JWT nuevo es verificable y corresponde al usuario original.
        assertThat(jwtDecoder.decode(renewed.accessToken()).getSubject()).isEqualTo(user.getId().toString());
    }

    @Test
    void reuseCommitsFamilyRevocationBeforeThrowingAndPreservesOtherFamilies() {
        User user = persistUser(UserRole.USER);
        LoginRequest credentials = new LoginRequest(user.getEmail(), PASSWORD);
        TokenResponse first = authService.login(credentials);
        TokenResponse otherFamily = authService.login(credentials);
        TokenResponse latest = authService.refresh(first.refreshToken());

        // El refresh consumido activa el rechazo específico de reutilización.
        assertThatThrownBy(() -> authService.refresh(first.refreshToken()))
                .isInstanceOf(InvalidRefreshTokenException.class);
        // Esta lectura posterior al error demuestra que la revocación no sufrió rollback.
        assertThat(storedToken(user, first.refreshToken()).getRevokedAt()).isEqualTo(NOW);
        // También se revoca el descendiente más reciente del grupo comprometido.
        assertThat(storedToken(user, latest.refreshToken()).getRevokedAt()).isEqualTo(NOW);
        // Otro grupo del mismo usuario conserva su vigencia.
        assertThat(storedToken(user, otherFamily.refreshToken()).isUsableAt(NOW)).isTrue();
        // El descendiente revocado ya no permite renovar.
        assertThatThrownBy(() -> authService.refresh(latest.refreshToken()))
                .isInstanceOf(InvalidRefreshTokenException.class);
    }

    @ParameterizedTest
    @ValueSource(strings = { "expired", "revoked", "unknown", "deleted" })
    void refreshRejectsUnavailableTokenWithoutCreatingRows(String state) {
        User user = persistUser(UserRole.USER);
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        String secret = login.refreshToken();
        if (state.equals("expired")) {
            doReturn(NOW.plusSeconds(604800)).when(clock).instant();
        } else if (state.equals("revoked")) {
            authService.logout(secret);
        } else if (state.equals("unknown")) {
            secret = "unknown-secret";
        } else {
            userRepository.deleteById(user.getId());
        }
        Long before = jdbc.queryForObject("select count(*) from refresh_tokens where user_id = ?",
                Long.class, user.getId());
        String presentedSecret = secret;
        // Todos los estados reciben el mismo error genérico de renovación.
        assertThatThrownBy(() -> authService.refresh(presentedSecret))
                .isInstanceOf(InvalidRefreshTokenException.class).hasMessage("Invalid refresh token");
        // Un rechazo no crea registros adicionales.
        assertThat(jdbc.queryForObject("select count(*) from refresh_tokens where user_id = ?",
                Long.class, user.getId())).isEqualTo(before);
    }

    @Test
    void refreshRejectsSubsecondRemainderWithoutConsumingToken() {
        User user = persistUser(UserRole.USER);
        doReturn(1L).when(authProperties).refreshTokenLifetimeSeconds();
        doReturn(NOW.plusMillis(250)).when(clock).instant();
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        doReturn(NOW.plusMillis(1050)).when(clock).instant();

        // La sesión vence a 01.250 pero ya no cabe un exp entero posterior al iat de 01.000.
        assertThatThrownBy(() -> authService.refresh(login.refreshToken()))
                .isInstanceOf(InvalidRefreshTokenException.class);
        // El rechazo temporal no consume el secreto ni se traduce en un error interno.
        assertThat(storedToken(user, login.refreshToken()).getUsedAt()).isNull();
    }

    @Test
    void failedRefreshIssuanceRollsBackAndOriginalSecretCanBeRetried() {
        User user = persistUser(UserRole.USER);
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        IllegalStateException failure = new IllegalStateException("Simulated refresh issuance failure");
        doThrow(failure).doCallRealMethod().when(accessTokenService).issue(eq(user.getId()), any(Instant.class));

        // Un fallo imprevisto mantiene la semántica de rollback normal.
        assertThatThrownBy(() -> authService.refresh(login.refreshToken())).isSameAs(failure);
        // El secreto anterior sigue sin consumir y no se ha revocado.
        assertThat(storedToken(user, login.refreshToken()).isUsableAt(NOW)).isTrue();
        TokenResponse retry = authService.refresh(login.refreshToken());
        // El reintento emite un nuevo JWT real y un nuevo secreto.
        assertThat(jwtDecoder.decode(retry.accessToken()).getSubject()).isEqualTo(user.getId().toString());
    }

    @ParameterizedTest
    @ValueSource(strings = { "active", "used", "expired", "revoked" })
    void logoutKnownTokenIsIdempotentAndOnlyRevokesItsFamily(String state) {
        User user = persistUser(UserRole.USER);
        LoginRequest request = new LoginRequest(user.getEmail(), PASSWORD);
        TokenResponse login = authService.login(request);
        TokenResponse other = authService.login(request);
        if (state.equals("used")) {
            authService.refresh(login.refreshToken());
        } else if (state.equals("expired")) {
            doReturn(NOW.plusSeconds(604800)).when(clock).instant();
        } else if (state.equals("revoked")) {
            authService.logout(login.refreshToken());
        }
        authService.logout(login.refreshToken());
        Instant originalRevocation = storedToken(user, login.refreshToken()).getRevokedAt();
        authService.logout(login.refreshToken());

        // Repetir el cierre conserva la primera revocación.
        assertThat(storedToken(user, login.refreshToken()).getRevokedAt()).isEqualTo(originalRevocation).isNotNull();
        // Otro grupo no se revoca, aunque ambos puedan haber caducado por el reloj de esta prueba.
        assertThat(storedToken(user, other.refreshToken()).getRevokedAt()).isNull();
    }

    @Test
    void secretsAreNotNormalizedAndUnknownLogoutIsRejected() {
        User user = persistUser(UserRole.USER);
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        // Un espacio añadido convierte el texto en otro secreto.
        assertThatThrownBy(() -> authService.refresh(" " + login.refreshToken()))
                .isInstanceOf(InvalidRefreshTokenException.class);
        // Logout tampoco recorta el secreto ni acepta uno desconocido.
        assertThatThrownBy(() -> authService.logout(login.refreshToken() + " "))
                .isInstanceOf(InvalidRefreshTokenException.class);
        // El secreto original sigue vigente después de ambos rechazos.
        assertThat(storedToken(user, login.refreshToken()).isUsableAt(NOW)).isTrue();
    }

    @ParameterizedTest
    @ValueSource(strings = { "refresh", "logout" })
    void refreshAndLogoutRespectTheSharedUserLock(String operation) throws Exception {
        User user = persistUser(UserRole.USER);
        TokenResponse login = authService.login(new LoginRequest(user.getEmail(), PASSWORD));
        CountDownLatch locked = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        TransactionTemplate transaction = new TransactionTemplate(transactionManager);
        transaction.setTimeout(10);
        try (var executor = Executors.newSingleThreadExecutor()) {
            var owner = executor.submit(() -> transaction.executeWithoutResult(status -> {
                userRepository.findByIdAndCompanyIdForUpdate(user.getId(), companyId).orElseThrow();
                locked.countDown();
                awaitRelease(release);
            }));
            try {
                // La operación competidora empieza solo cuando User ya está bloqueado.
                assertThat(locked.await(10, TimeUnit.SECONDS)).isTrue();
                // Las operaciones de renovación y cierre comparten el mismo bloqueo NOWAIT.
                assertThatThrownBy(() -> {
                    if (operation.equals("refresh")) {
                        authService.refresh(login.refreshToken());
                    } else {
                        authService.logout(login.refreshToken());
                    }
                }).isInstanceOf(PessimisticLockingFailureException.class);
                // El intento rechazado no consume ni revoca el secreto.
                assertThat(storedToken(user, login.refreshToken()).isUsableAt(NOW)).isTrue();
            } finally {
                release.countDown();
                owner.get(10, TimeUnit.SECONDS);
            }
        }
        TokenResponse renewed = authService.refresh(login.refreshToken());
        // Después de liberar el bloqueo el mismo secreto permite una renovación normal.
        assertThat(jwtDecoder.decode(renewed.accessToken()).getSubject()).isEqualTo(user.getId().toString());
    }

    private RefreshToken storedToken(User user, String secret) {
        return refreshTokenRepository.findByTokenHashAndUser_Id(refreshTokenHasher.hash(secret), user.getId())
                .orElseThrow();
    }

    /** Prepara una identidad confirmada y una empresa exclusiva de la prueba. */
    private User persistUser(UserRole role) {
        Company company = companyRepository.saveAndFlush(new Company("Empresa de prueba de login"));
        companyId = company.getId();
        return userRepository.saveAndFlush(new User("Ana", "García", UUID.randomUUID() + "@example.com",
                passwordEncoder.encode(PASSWORD), role, company));
    }

    /** Coordina la transacción concurrente sin pausas arbitrarias y con un tiempo máximo. */
    private void awaitRelease(CountDownLatch release) {
        try {
            if (!release.await(10, TimeUnit.SECONDS)) {
                throw new IllegalStateException("Timed out waiting for password change release");
            }
        } catch (InterruptedException exception) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("Password change test interrupted", exception);
        }
    }

    /** Sustituye el reloj del backend para comprobar las fechas de los tokens exactamente. */
    @TestConfiguration(proxyBeanMethods = false)
    static class FixedClockConfiguration {

        @Bean
        @Primary
        Clock authServiceTestClock() {
            return Clock.fixed(NOW, ZoneOffset.UTC);
        }
    }
}
