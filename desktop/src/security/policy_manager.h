#pragma once

#include "security/policy_publication_result.h"
#include "security/policy_snapshot.h"
#include "security/policy_validation_error.h"
#include "security/security_context.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace sbp {

// Valida políticas y gestiona el contexto y su estado local de seguridad.
class PolicyManager final {
public:
    // Mide tiempo transcurrido sin depender de los cambios de hora del sistema.
    using Clock = std::chrono::steady_clock;

    // Función sin argumentos que proporciona el instante monotónico actual.
    using NowFunction = std::function<Clock::time_point()>;

    // Contexto, política y vigencia se comparten juntos mediante una referencia de solo lectura.
    struct State {
        SecurityContext context;

        // Vacío hasta recibir una política completa y válida para este contexto.
        std::shared_ptr<const PolicySnapshot> policy{};

        // Instante local de validación correcta; no procede de createdAt ni updatedAt.
        std::optional<Clock::time_point> validatedAt{};
    };

    // Utiliza el reloj monotónico real cuando no se proporciona uno de pruebas.
    PolicyManager() = default;

    // Permite proporcionar un reloj controlado; la función no puede estar vacía.
    explicit PolicyManager(NowFunction now);

    // REST/Auth lo llamará tras confirmar una sesión y una instalación autorizadas.
    // Inicia un contexto con generación nueva, sin reutilizar la política del anterior.
    // Devuelve nullopt si los IDs no son positivos o se agotan las generaciones disponibles.
    [[nodiscard]] std::optional<SecurityContext> BeginContext(
        std::int64_t deviceId, std::int64_t companyId);

    // Indica si existe un estado actual y su contexto coincide con el recibido.
    // El resultado describe esta consulta; al publicar se comprobará de nuevo bajo el mutex.
    [[nodiscard]] bool IsCurrentContext(const SecurityContext& context) const;

    // Retira el estado solo si pertenece al contexto recibido; devuelve true si lo cerró.
    // Un cierre tardío de otro contexto no modifica el actual ni reinicia las generaciones.
    [[nodiscard]] bool EndContext(const SecurityContext& context);

    // Recibe una política propia y el contexto que originó su consulta.
    // Valida fuera del mutex y comprueba de nuevo el contexto antes de publicar el estado completo.
    // Un rechazo conserva el estado anterior y no renueva su vigencia.
    [[nodiscard]] PolicyPublicationResult PublishPolicy(
        const SecurityContext& context, PolicySnapshot policy);

    // Obtiene el estado completo de solo lectura; devuelve un puntero vacío si no existe contexto.
    // Conservarlo no renueva su vigencia ni sustituye las comprobaciones actuales de autorización.
    [[nodiscard]] std::shared_ptr<const State> ReadState() const;

    // Comprueba el conjunto y devuelve el primer error, o None si todos los datos son válidos.
    // La compañía esperada debe proceder del contexto de instalación, no de la política.
    // REST debe exigir las fechas y convertirlas correctamente a Timestamp antes de construir el snapshot.
    // Validar no publica el snapshot ni acredita por sí solo sesión, autorización o vigencia.
    [[nodiscard]] PolicyValidationError Validate(
        const PolicySnapshot& policy, std::int64_t expectedCompanyId) const;

private:
    // Comprueba identificadores positivos y correspondencia de la compañía.
    [[nodiscard]] static PolicyValidationError ValidateIdentity(
        const PolicySnapshot& policy, std::int64_t expectedCompanyId) noexcept;

    // Comprueba codificación, contenido y longitud del nombre según el contrato backend.
    [[nodiscard]] static PolicyValidationError ValidateName(const std::string& name);

    // Exige modos conocidos incluso cuando las funcionalidades estén desactivadas.
    [[nodiscard]] static PolicyValidationError ValidateModes(const PolicySnapshot& policy) noexcept;

    // Comprueba todas las reglas de dominio y rechaza duplicados normalizados.
    [[nodiscard]] static PolicyValidationError ValidateUrlRules(const UrlFilteringConfig& config);

    // Comprueba todas las reglas de extensión y rechaza duplicados normalizados.
    [[nodiscard]] static PolicyValidationError ValidateDownloadRules(const DownloadControlConfig& config);

    // Convierte UTF-8 válido a minúsculas Unicode con el locale raíz de ICU.
    // Solo modifica lowercased si la conversión tiene éxito; no elimina acentos.
    [[nodiscard]] static bool ToLowerUtf8(const std::string& value, std::string& lowercased);

    // Lee un punto de código UTF-8 válido y avanza offset solo si la lectura tiene éxito.
    // Devuelve false si no hay un carácter completo válido en la posición indicada.
    [[nodiscard]] static bool ReadUtf8CodePoint(
        const std::string& value, std::size_t& offset, char32_t& codePoint) noexcept;

    // Se invocará fuera del mutex; el reloj inyectado debe admitir llamadas concurrentes.
    NowFunction now_ = &Clock::now;

    // Protege la obtención y sustitución del estado y la asignación de generaciones.
    mutable std::mutex stateMutex_;

    // Empieza en cero; las generaciones publicadas serán positivas y no se reutilizarán.
    std::uint64_t lastGeneration_ = 0;

    // Vacío al construir el gestor: todavía no existe un contexto actual.
    std::shared_ptr<const State> state_;
};

}  // namespace sbp
