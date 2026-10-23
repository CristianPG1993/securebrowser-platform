package com.securebrowser.platform.auth;

import java.time.Clock;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.UUID;

import com.securebrowser.platform.auth.dto.LoginRequest;
import com.securebrowser.platform.auth.dto.TokenResponse;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRepository;

import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Coordina la comprobación de credenciales y la creación de grupos de login. */
@Service
public class AuthService {

    // Busca y bloquea la identidad del usuario que intenta iniciar sesión.
    private final UserRepository userRepository;
    // Persiste únicamente el hash del refresh y los datos de su grupo.
    private final RefreshTokenRepository refreshTokenRepository;
    // Compara la contraseña recibida con el hash BCrypt del usuario.
    private final PasswordEncoder passwordEncoder;
    // Emite el JWT de acceso con los claims y límites acordados.
    private final AccessTokenService accessTokenService;
    // Genera el secreto de renovación que se entregará al cliente.
    private final RefreshTokenGenerator refreshTokenGenerator;
    // Calcula la huella del secreto que se guardará en PostgreSQL.
    private final RefreshTokenHasher refreshTokenHasher;
    // Proporciona la duración absoluta configurada del grupo.
    private final AuthProperties properties;
    // Proporciona la hora del backend, sustituible en las pruebas.
    private final Clock clock;

    /** Recibe mediante inyección las piezas de autenticación ya implementadas. */
    public AuthService(UserRepository userRepository, RefreshTokenRepository refreshTokenRepository,
            PasswordEncoder passwordEncoder, AccessTokenService accessTokenService,
            RefreshTokenGenerator refreshTokenGenerator, RefreshTokenHasher refreshTokenHasher,
            AuthProperties properties, Clock clock) {
        this.userRepository = userRepository;
        this.refreshTokenRepository = refreshTokenRepository;
        this.passwordEncoder = passwordEncoder;
        this.accessTokenService = accessTokenService;
        this.refreshTokenGenerator = refreshTokenGenerator;
        this.refreshTokenHasher = refreshTokenHasher;
        this.properties = properties;
        this.clock = clock;
    }

    /** Comprueba unas credenciales previamente validadas y crea una sesión de forma atómica. */
    @Transactional
    public TokenResponse login(LoginRequest request) {
        // El bloqueo se obtiene antes de verificar la contraseña y se mantiene hasta el commit.
        User user = userRepository.findByEmailForUpdate(request.email())
                .orElseThrow(InvalidCredentialsException::new);
        if (!passwordEncoder.matches(request.password(), user.getPasswordHash())) {
            throw new InvalidCredentialsException();
        }

        // La precisión coincide con las fechas persistidas por RefreshToken en PostgreSQL.
        Instant createdAt = clock.instant().truncatedTo(ChronoUnit.MICROS);
        Instant expiresAt = createdAt.plusSeconds(properties.refreshTokenLifetimeSeconds());
        String refreshSecret = refreshTokenGenerator.generate();
        RefreshToken refreshToken = new RefreshToken(user, refreshTokenHasher.hash(refreshSecret),
                UUID.randomUUID(), createdAt, expiresAt);
        refreshTokenRepository.save(refreshToken);

        Jwt accessToken = accessTokenService.issue(user.getId(), expiresAt);
        // Spring confirma la transacción antes de entregar el resultado al llamador del servicio.
        return TokenResponse.from(accessToken, refreshSecret);
    }

    /** Rota un secreto vigente; la detección de reutilización confirma la revocación antes del error. */
    @Transactional(noRollbackFor = InvalidRefreshTokenException.class)
    public TokenResponse refresh(String rawToken) {
        RefreshToken previous = lockRefreshToken(rawToken);
        Instant now = clock.instant().truncatedTo(ChronoUnit.MICROS);

        if (previous.getUsedAt() != null) {
            revokeFamily(previous, now);
            throw new InvalidRefreshTokenException();
        }
        if (!previous.isUsableAt(now)
                || !previous.getExpiresAt().truncatedTo(ChronoUnit.SECONDS)
                        .isAfter(now.truncatedTo(ChronoUnit.SECONDS))) {
            throw new InvalidRefreshTokenException();
        }

        // Firma antes de consumir el secreto; cualquier fallo imprevisto revierte toda la operación.
        Jwt accessToken;
        try {
            accessToken = accessTokenService.issue(previous.getUser().getId(), previous.getExpiresAt());
        } catch (IllegalArgumentException exception) {
            // El límite puede alcanzarse entre la comprobación anterior y la emisión.
            if (!previous.getExpiresAt().truncatedTo(ChronoUnit.SECONDS)
                    .isAfter(clock.instant().truncatedTo(ChronoUnit.SECONDS))) {
                throw new InvalidRefreshTokenException();
            }
            throw exception;
        }
        String nextSecret = refreshTokenGenerator.generate();
        previous.markUsed(now);
        refreshTokenRepository.save(new RefreshToken(previous.getUser(), refreshTokenHasher.hash(nextSecret),
                previous.getFamilyId(), now, previous.getExpiresAt()));
        return TokenResponse.from(accessToken, nextSecret);
    }

    /** Un secreto conocido cierra solo su grupo, incluso si ya fue consumido, revocado o caducó. */
    @Transactional
    public void logout(String rawToken) {
        RefreshToken token = lockRefreshToken(rawToken);
        revokeFamily(token, clock.instant().truncatedTo(ChronoUnit.MICROS));
    }

    /** Evita leer una entidad RefreshToken potencialmente obsoleta antes de obtener el bloqueo. */
    private RefreshToken lockRefreshToken(String rawToken) {
        if (rawToken == null || rawToken.isEmpty()) {
            throw new InvalidRefreshTokenException();
        }
        String hash = refreshTokenHasher.hash(rawToken);
        Long userId = refreshTokenRepository.findUserIdByTokenHash(hash)
                .orElseThrow(InvalidRefreshTokenException::new);
        User user = userRepository.findByIdForUpdate(userId)
                .orElseThrow(InvalidRefreshTokenException::new);
        return refreshTokenRepository.findByTokenHashAndUser_Id(hash, user.getId())
                .orElseThrow(InvalidRefreshTokenException::new);
    }

    private void revokeFamily(RefreshToken token, Instant now) {
        refreshTokenRepository.findByUser_IdAndFamilyIdAndRevokedAtIsNullOrderByIdAsc(
                token.getUser().getId(), token.getFamilyId()).forEach(member -> member.revoke(now));
    }
}
