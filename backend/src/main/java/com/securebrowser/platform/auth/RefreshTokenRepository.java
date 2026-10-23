package com.securebrowser.platform.auth;

import java.util.List;
import java.util.Optional;
import java.util.UUID;

import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

/** Proporciona los registros necesarios para revocar la renovación de un usuario. */
public interface RefreshTokenRepository extends JpaRepository<RefreshToken, Long> {

    /** Proyecta solo el ID: el estado del token se leerá después de bloquear User. */
    @Query("select r.user.id from RefreshToken r where r.tokenHash = :hash")
    Optional<Long> findUserIdByTokenHash(@Param("hash") String hash);

    Optional<RefreshToken> findByTokenHashAndUser_Id(String tokenHash, Long userId);

    /** La modificación de estos registros exige mantener previamente el bloqueo de User. */
    List<RefreshToken> findByUser_IdAndFamilyIdAndRevokedAtIsNullOrderByIdAsc(Long userId, UUID familyId);

    /**
     * Recupera todos los grupos no revocados, incluidos tokens consumidos o caducados.
     * El servicio debe bloquear antes el User y llamar a revoke dentro de esa transacción.
     */
    List<RefreshToken> findByUser_IdAndRevokedAtIsNullOrderByIdAsc(Long userId);
}
