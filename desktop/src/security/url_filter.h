#pragma once

#include "security/policy_manager.h"
#include "security/security_decision.h"
#include "security/url_target.h"
#include "security/url_filtering_config.h"

#include <string>

namespace sbp {

// Evalúa un destino web ya interpretado, sin realizar red, modificar estado ni emitir eventos.
class UrlFilter final {
public:
    // Recibe una lectura reciente de PolicyManager::ReadPolicy para la operación solicitante.
    // Comprueba disponibilidad, datos del destino y reglas, conservando el primer bloqueo.
    // No consulta el reloj ni renueva la política: utiliza el estado coherente recibido.
    // El resultado no sustituye la comprobación final de contexto y autorización al aplicarlo.
    [[nodiscard]] SecurityDecision Evaluate(
        const PolicyManager::PolicyReadResult& policy,
        const UrlTarget& target) const;

private:
    // Devuelve Allowed solo para superar esta comprobación, sin autorizar todavía el destino.
    [[nodiscard]] static SecurityReason CheckPolicyAvailability(
        const PolicyManager::PolicyReadResult& policy);

    // Comprueba el resultado del adaptador y los requisitos generales del destino web.
    // Allowed permite continuar con las reglas; no significa que el destino esté autorizado.
    [[nodiscard]] static SecurityReason CheckTarget(const UrlTarget& target);

    // Compara un host de dominio canónico con una regla previamente validada.
    // Normaliza una copia de la regla sin modificar el snapshot ni introducir alias IDN.
    [[nodiscard]] static bool MatchesDomain(const std::string& host, const std::string& rule);

    // Aplica activación, restricción de IP y modo sobre un destino ya comprobado.
    // La configuración debe proceder de una política validada por PolicyManager.
    [[nodiscard]] static SecurityReason EvaluateRules(
        const UrlFilteringConfig& config, const UrlTarget& target);
};

}  // namespace sbp
