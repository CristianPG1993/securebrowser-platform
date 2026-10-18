package com.securebrowser.platform.user;

import org.springframework.data.jpa.repository.JpaRepository;

/** Proporciona acceso a la persistencia de User mediante Spring Data JPA. */
public interface UserRepository extends JpaRepository<User, Long> {
}
