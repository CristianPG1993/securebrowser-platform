#pragma once

#include "security/policy_availability.h"
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
#include <vector>

namespace sbp {

// Valida políticas y gestiona el contexto y su estado local de seguridad.
class PolicyManager final {
public:
    // Mide tiempo transcurrido sin depender de los cambios de hora del sistema.
    using Clock = std::chrono::steady_clock;

    // Función sin argumentos que proporciona el instante monotónico actual.
    using NowFunction = std::function<Clock::time_point()>;

    // Recibe el contexto que deja de estar vigente al cerrarlo o sustituirlo por otro.
    // Se invocará fuera del mutex, en el hilo que realiza ese cambio; el receptor debe ser breve.
    using ContextRetiredCallback = std::function<void(const SecurityContext&)>;

    // Conservar esta suscripción mantiene activo el receptor; el gestor solo guardará una referencia débil.
    // Liberarla impide entregas nuevas, pero no interrumpe una notificación que ya tomó su copia.
    using ContextRetiredSubscription = std::shared_ptr<const ContextRetiredCallback>;

    // La composición fija la vigencia al construir el gestor; no cambia al publicar otra política.
    enum class Validity {
        FifteenMinutes,
        Unlimited
    };

    // Contexto, política y vigencia se comparten juntos mediante una referencia de solo lectura.
    struct State {
        SecurityContext context;

        // Vacío hasta recibir una política completa y válida para este contexto.
        std::shared_ptr<const PolicySnapshot> policy{};

        // Instante local de validación correcta; no procede de createdAt ni updatedAt.
        std::optional<Clock::time_point> validatedAt{};
    };

    // La disponibilidad corresponde exactamente al estado compartido que acompaña al resultado.
    struct PolicyReadResult {
        PolicyAvailability availability = PolicyAvailability::InvalidContext;
        std::shared_ptr<const State> state{};
    };

    // Utiliza el reloj monotónico real y la vigencia predeterminada de 15 minutos.
    PolicyManager() = default;

    // Permite elegir explícitamente otra vigencia; la aplicación solo utiliza Unlimited en modo demo.
    explicit PolicyManager(Validity validity);

    // Permite proporcionar un reloj controlado; conserva 15 minutos si no se especifica otra vigencia.
    // La función no puede estar vacía y la opción de vigencia debe ser conocida.
    explicit PolicyManager(NowFunction now, Validity validity = Validity::FifteenMinutes);

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

    // Registra un receptor no vacío para las retiradas posteriores de contexto.
    // No avisa por cambios de política ni caducidad; el receptor encolará su trabajo en el hilo adecuado.
    // Debe conservarse la suscripción antes de iniciar las operaciones que necesiten cancelación.
    [[nodiscard]] ContextRetiredSubscription SubscribeToContextRetirement(ContextRetiredCallback callback);

    // Recibe una política propia y el contexto que originó su consulta.
    // Valida fuera del mutex y comprueba de nuevo el contexto antes de publicar el estado completo.
    // Un rechazo conserva el estado anterior y no renueva su vigencia.
    [[nodiscard]] PolicyPublicationResult PublishPolicy(
        const SecurityContext& context, PolicySnapshot policy);

    // Obtiene el estado completo de solo lectura; devuelve un puntero vacío si no existe contexto.
    // Conservarlo no renueva su vigencia ni sustituye las comprobaciones actuales de autorización.
    [[nodiscard]] std::shared_ptr<const State> ReadState() const;

    // Comprueba contexto y vigencia sin renovar el plazo; solo Available indica una política utilizable.
    // Conserva la referencia al estado comprobado y no evalúa reglas de navegación o descarga.
    [[nodiscard]] PolicyReadResult ReadPolicy(const SecurityContext& context) const;

    // Comprueba el conjunto y devuelve el primer error, o None si todos los datos son válidos.
    // La compañía esperada debe proceder del contexto de instalación, no de la política.
    // REST debe exigir las fechas y convertirlas correctamente a Timestamp antes de construir el snapshot.
    // Validar no publica el snapshot ni acredita por sí solo sesión, autorización o vigencia.
    [[nodiscard]] PolicyValidationError Validate(
        const PolicySnapshot& policy, std::int64_t expectedCompanyId) const;

private:
    // Copia los receptores activos y elimina referencias caducadas antes de retirar el contexto.
    // El llamador debe mantener stateMutex_; las funciones copiadas se ejecutarán después de liberarlo.
    [[nodiscard]] std::vector<ContextRetiredSubscription> CollectContextRetiredCallbacks();

    // Entrega el contexto retirado fuera del mutex y contiene las excepciones de cada receptor.
    // Una entrega no puede modificar el contexto comunicado ni impedir las restantes notificaciones.
    static void NotifyContextRetired(
        const SecurityContext& context, const std::vector<ContextRetiredSubscription>& callbacks) noexcept;

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

    // Es una opción inmutable de este gestor, no una excepción por URL ni una renovación automática.
    const Validity validity_ = Validity::FifteenMinutes;

    // Protege el estado, la asignación de generaciones y el registro de receptores de retirada.
    mutable std::mutex stateMutex_;

    // No conserva a los receptores cuando quien se suscribió deja de necesitar las notificaciones.
    std::vector<std::weak_ptr<const ContextRetiredCallback>> contextRetiredCallbacks_;

    // Empieza en cero; las generaciones publicadas serán positivas y no se reutilizarán.
    std::uint64_t lastGeneration_ = 0;

    // Vacío al construir el gestor: todavía no existe un contexto actual.
    std::shared_ptr<const State> state_;
};

}  // namespace sbp
