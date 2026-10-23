package com.securebrowser.platform.policy;

import java.util.ArrayList;
import java.util.List;
import com.securebrowser.platform.api.PageResponse;
import com.securebrowser.platform.policy.dto.PolicyResponse;
import com.securebrowser.platform.policy.dto.PolicyUpdateRequest;
import com.securebrowser.platform.security.AuthenticatedUser;
import com.securebrowser.platform.user.UserRole;
import org.springframework.data.domain.PageRequest;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Isolation;
import org.springframework.transaction.annotation.Transactional;

/** Conserva autorización, pertenencia y cambios atómicos en el límite transaccional. */
@Service
public class PolicyService {
    private final PolicyRepository repository;

    public PolicyService(PolicyRepository repository) {
        this.repository = repository;
    }

    /** El mismo snapshot abarca página, total y las dos colecciones cargadas de forma diferida. */
    @Transactional(readOnly = true, isolation = Isolation.REPEATABLE_READ)
    public PageResponse<PolicyResponse> list(AuthenticatedUser actor, int page, int size) {
        requireAdmin(actor);
        if (page < 0 || size < 1 || size > 100) throw new InvalidPolicyRequestException();
        return PageResponse.from(repository.findByCompany_IdOrderByIdAsc(actor.companyId(),
                PageRequest.of(page, size)).map(PolicyResponse::from));
    }

    @Transactional(readOnly = true, isolation = Isolation.REPEATABLE_READ)
    public PolicyResponse get(AuthenticatedUser actor, Long id) {
        requireAdmin(actor);
        requireId(id);
        return PolicyResponse.from(repository.findByIdAndCompany_Id(id, actor.companyId())
                .orElseThrow(PolicyNotFoundException::new));
    }

    @Transactional
    public PolicyResponse update(AuthenticatedUser actor, Long id, PolicyUpdateRequest request) {
        requireAdmin(actor);
        requireId(id);
        if (request == null) throw new InvalidPolicyRequestException();
        Policy policy = repository.findByIdAndCompanyIdForUpdate(id, actor.companyId())
                .orElseThrow(PolicyNotFoundException::new);

        // Valida y normaliza todo en una entidad transitoria antes de tocar el estado persistente.
        Policy candidate;
        try {
            candidate = new Policy(request.name() == null ? policy.getName() : request.name(), policy.getCompany());
            candidate.configureUrlFiltering(
                    request.urlFilteringEnabled() == null ? policy.isUrlFilteringEnabled() : request.urlFilteringEnabled(),
                    request.urlFilteringMode() == null ? policy.getUrlFilteringMode() : request.urlFilteringMode());
            candidate.configureDownloadControl(
                    request.downloadControlEnabled() == null ? policy.isDownloadControlEnabled() : request.downloadControlEnabled(),
                    request.downloadControlMode() == null ? policy.getDownloadControlMode() : request.downloadControlMode());
            if (request.urlRules() != null) request.urlRules().forEach(candidate::addUrlRule);
            if (request.downloadRules() != null) request.downloadRules().forEach(candidate::addDownloadRule);
        } catch (IllegalArgumentException exception) {
            throw new InvalidPolicyRequestException();
        }

        policy.changeName(candidate.getName());
        policy.configureUrlFiltering(candidate.isUrlFilteringEnabled(), candidate.getUrlFilteringMode());
        policy.configureDownloadControl(candidate.isDownloadControlEnabled(), candidate.getDownloadControlMode());
        if (request.urlRules() != null) {
            List<String> desired = candidate.getUrlRules().stream().map(UrlRule::getDomain).toList();
            // Conserva las filas comunes: evita insertar un duplicado antes de borrar su antigua fila.
            for (UrlRule rule : new ArrayList<>(policy.getUrlRules())) {
                if (!desired.contains(rule.getDomain())) policy.removeUrlRule(rule);
            }
            for (String domain : desired) {
                if (policy.getUrlRules().stream().noneMatch(rule -> rule.getDomain().equals(domain))) {
                    policy.addUrlRule(domain);
                }
            }
        }
        if (request.downloadRules() != null) {
            List<String> desired = candidate.getDownloadRules().stream().map(DownloadRule::getExtension).toList();
            for (DownloadRule rule : new ArrayList<>(policy.getDownloadRules())) {
                if (!desired.contains(rule.getExtension())) policy.removeDownloadRule(rule);
            }
            for (String extension : desired) {
                if (policy.getDownloadRules().stream().noneMatch(rule -> rule.getExtension().equals(extension))) {
                    policy.addDownloadRule(extension);
                }
            }
        }
        // Dispara la auditoría y detecta errores de persistencia antes de construir la respuesta.
        repository.flush();
        return PolicyResponse.from(policy);
    }

    private static void requireAdmin(AuthenticatedUser actor) {
        if (actor == null || actor.role() != UserRole.ADMIN) {
            throw new AccessDeniedException("Policy administration requires ADMIN role");
        }
    }

    private static void requireId(Long id) {
        if (id == null || id <= 0) throw new InvalidPolicyRequestException();
    }
}
