package com.securebrowser.platform.demo;

import java.time.Clock;
import java.time.Instant;
import java.time.temporal.ChronoUnit;
import java.util.List;

import com.securebrowser.platform.auth.RefreshTokenGenerator;
import com.securebrowser.platform.auth.RefreshTokenHasher;
import com.securebrowser.platform.company.Company;
import com.securebrowser.platform.device.Device;
import com.securebrowser.platform.enrollment.EnrollmentToken;
import com.securebrowser.platform.license.License;
import com.securebrowser.platform.policy.Policy;
import com.securebrowser.platform.user.User;
import com.securebrowser.platform.user.UserRole;

import jakarta.persistence.EntityManager;
import jakarta.persistence.PersistenceContext;
import org.springframework.boot.ApplicationArguments;
import org.springframework.boot.ApplicationRunner;
import org.springframework.context.annotation.Profile;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Component;
import org.springframework.transaction.annotation.Transactional;

/** Crea una única fixture demo completa, atómica y conservada entre reinicios. */
@Component
@Profile("demo")
public class DemoDataInitializer implements ApplicationRunner {
    public static final String ADMIN_EMAIL = "admin@demo.local";
    public static final String USER_EMAIL = "user@demo.local";
    public static final String DEVICE_IDENTIFIER = "b7eacb45-4aba-4a5a-8c0c-633a61294af1";

    @PersistenceContext
    private EntityManager entityManager;
    private final DemoProperties properties;
    private final PasswordEncoder encoder;
    private final RefreshTokenGenerator generator;
    private final RefreshTokenHasher hasher;
    private final Clock clock;

    public DemoDataInitializer(DemoProperties properties, PasswordEncoder encoder,
            RefreshTokenGenerator generator, RefreshTokenHasher hasher, Clock clock) {
        this.properties = properties;
        this.encoder = encoder;
        this.generator = generator;
        this.hasher = hasher;
        this.clock = clock;
    }

    @Override
    @Transactional
    public void run(ApplicationArguments arguments) {
        long devices = entityManager.createQuery(
                "select count(d) from Device d where d.deviceIdentifier = :identifier", Long.class)
                .setParameter("identifier", DEVICE_IDENTIFIER).getSingleResult();
        if (devices != 0) {
            return;
        }
        long existingUsers = entityManager.createQuery(
                "select count(u) from User u where u.email in :emails", Long.class)
                .setParameter("emails", List.of(ADMIN_EMAIL, USER_EMAIL)).getSingleResult();
        if (existingUsers != 0) {
            throw new IllegalStateException("Demo identities already exist without the demo device");
        }

        Company company = new Company("SBP Demo");
        entityManager.persist(company);
        User admin = new User("Admin", "Demo", ADMIN_EMAIL,
                encoder.encode(properties.adminPassword()), UserRole.ADMIN, company);
        User user = new User("Usuario", "Demo", USER_EMAIL,
                encoder.encode(properties.userPassword()), UserRole.USER, company);
        entityManager.persist(admin);
        entityManager.persist(user);
        License license = new License(company, 5);
        entityManager.persist(license);
        Policy policy = new Policy("Política demo", company);
        policy.addUrlRule("blocked.example");
        policy.addDownloadRule("exe");
        entityManager.persist(policy);

        Instant now = clock.instant().truncatedTo(ChronoUnit.MICROS);
        EnrollmentToken enrollment = new EnrollmentToken(license, user, policy,
                hasher.hash(generator.generate()), now, now.plusSeconds(3600));
        enrollment.markUsed(now);
        entityManager.persist(enrollment);
        entityManager.persist(new Device(DEVICE_IDENTIFIER, "Desktop Demo", company, user, license, policy, enrollment));
    }
}
