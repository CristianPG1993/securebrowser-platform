package com.securebrowser.platform.auth;

import java.util.List;

import org.springframework.data.jpa.repository.JpaRepository;

/** Proporciona los registros necesarios para revocar la renovación de un usuario. */
public interface RefreshTokenRepository extends JpaRepository<RefreshToken, Long> {

    /**
     * Recupera todos los grupos no revocados, incluidos tokens consumidos o caducados.
     * El servicio debe bloquear antes el User y llamar a revoke dentro de esa transacción.
     */
    List<RefreshToken> findByUser_IdAndRevokedAtIsNullOrderByIdAsc(Long userId);
}
