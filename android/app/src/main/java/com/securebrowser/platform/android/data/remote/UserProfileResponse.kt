package com.securebrowser.platform.android.data.remote

import kotlinx.serialization.Serializable

/** Campos de GET /users/me que el Dashboard utiliza para mostrar la identidad y el rol. */
@Serializable
internal class UserProfileResponse(
    val name: String,
    val lastName: String,
    val email: String,
    val role: UserRole,
)

/** Roles que Spring Boot puede devolver en el perfil autenticado. */
@Serializable
internal enum class UserRole { ADMIN, USER }
