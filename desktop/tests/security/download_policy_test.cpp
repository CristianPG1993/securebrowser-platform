#include "security/download_policy.h"

#include <gtest/gtest.h>

#include <chrono>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sbp {

// Un nombre válido no permite descargar cuando no existe un contexto de seguridad.
TEST(DownloadPolicyTest, BlocksWithoutSecurityContext) {
    const DownloadPolicy evaluator;
    const PolicyManager::PolicyReadResult policy{};

    const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Un contexto válido sigue bloqueado hasta disponer de una política publicada.
TEST(DownloadPolicyTest, BlocksCurrentContextWithoutPolicy) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const auto policy = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Unavailable, policy.availability);
    const DownloadPolicy evaluator;

    const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
}

// Desactivar el control omite las listas, incluso una ALLOWLIST vacía, con una política vigente.
TEST(DownloadPolicyTest, AllowsValidFileWithFreshPolicyAndControlDisabled) {
    // El reloj fijo mantiene vigente la política sin depender del tiempo real.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Allowlist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));

    const auto policy = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, policy.availability);
    const DownloadPolicy evaluator;

    const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

    EXPECT_EQ(SecurityAction::Allow, decision.Action());
    EXPECT_EQ(SecurityReason::Allowed, decision.Reason());
}

// Fija el tiempo transcurrido y el motivo esperado a ambos lados del límite de vigencia.
struct DownloadPolicyExpiryCase {
    const char* name;
    PolicyManager::Clock::duration elapsed;
    SecurityReason expectedReason;
};

class DownloadPolicyExpiryTest : public ::testing::TestWithParam<DownloadPolicyExpiryCase> {};

// La caducidad impide evaluar nuevas descargas con el control activado o desactivado.
TEST_P(DownloadPolicyExpiryTest, UsesAvailabilityAtExpiryBoundary) {
    const DownloadPolicyExpiryCase& testCase = GetParam();
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        PolicyManager::Clock::time_point now{};
        // now vive hasta después del gestor y su referencia solo se utiliza en este hilo.
        PolicyManager manager([&now] { return now; });
        const auto context = manager.BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());

        const PolicySnapshot snapshot(
            1, 20, "Office policy",
            UrlFilteringConfig(false, FilterMode::Denylist, {}),
            DownloadControlConfig(enabled, FilterMode::Denylist, {}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));

        now += testCase.elapsed;
        const auto policy = manager.ReadPolicy(*context);
        ASSERT_EQ(testCase.expectedReason == SecurityReason::Allowed
            ? PolicyAvailability::Available : PolicyAvailability::Expired, policy.availability);
        const DownloadPolicy evaluator;

        const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

        EXPECT_EQ(testCase.expectedReason == SecurityReason::Allowed
            ? SecurityAction::Allow : SecurityAction::Block, decision.Action());
        EXPECT_EQ(testCase.expectedReason, decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(
    LocalExpiry,
    DownloadPolicyExpiryTest,
    ::testing::Values(
        // Falta una unidad del reloj para los 15 minutos: la política todavía permite evaluar.
        DownloadPolicyExpiryCase{"JustBeforeDeadline",
            std::chrono::minutes{15} - PolicyManager::Clock::duration{1}, SecurityReason::Allowed},
        // Al alcanzar exactamente los 15 minutos, la política ya está caducada.
        DownloadPolicyExpiryCase{"AtDeadline", std::chrono::minutes{15}, SecurityReason::PolicyExpired},
        // Una unidad después del límite, la política sigue bloqueando por caducidad.
        DownloadPolicyExpiryCase{"AfterDeadline",
            std::chrono::minutes{15} + PolicyManager::Clock::duration{1}, SecurityReason::PolicyExpired}),
    // El nombre distingue cada límite temporal; la lambda no captura referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyExpiryCase>& info) {
        return std::string(info.param.name);
    });

// Cerrar el contexto impide nuevas evaluaciones autorizadas aunque hubiera una política publicada.
TEST(DownloadPolicyTest, BlocksEndedContext) {
    // El reloj fijo evita confundir el cierre del contexto con la caducidad de la política.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Allowlist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    ASSERT_TRUE(manager.EndContext(*context));

    const auto policy = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::InvalidContext, policy.availability);
    const DownloadPolicy evaluator;

    const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Una sesión nueva con el mismo Device y compañía no autoriza operaciones de la sesión anterior.
TEST(DownloadPolicyTest, BlocksContextReplacedByNewGeneration) {
    // El reloj fijo aísla la sustitución del contexto respecto a la caducidad.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());

    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Allowlist, {}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*previous, snapshot));

    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    ASSERT_NE(previous->Generation(), current->Generation());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*current, snapshot));
    ASSERT_EQ(PolicyAvailability::Available, manager.ReadPolicy(*current).availability);

    const auto policy = manager.ReadPolicy(*previous);
    ASSERT_EQ(PolicyAvailability::InvalidContext, policy.availability);
    const DownloadPolicy evaluator;

    const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(SecurityReason::InvalidContext, decision.Reason());
}

// Estas lecturas se construyen solo para comprobar defensas ante estados incompletos o incoherentes.
// Las evaluaciones normales reciben una lectura reciente de PolicyManager::ReadPolicy.
struct DownloadPolicyReadCase {
    const char* name;
    PolicyAvailability availability;
    bool hasState;
    bool hasPolicy;
    bool hasTimestamp;
    SecurityReason expectedReason;
};

class DownloadPolicyReadTest : public ::testing::TestWithParam<DownloadPolicyReadCase> {};

// La activación del control nunca convierte una lectura no utilizable en una autorización.
TEST_P(DownloadPolicyReadTest, RejectsIncompleteOrUnusableRead) {
    const DownloadPolicyReadCase& testCase = GetParam();
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        const auto snapshot = std::make_shared<const PolicySnapshot>(
            1, 20, "Office policy",
            UrlFilteringConfig(false, FilterMode::Denylist, std::vector<std::string>{}),
            DownloadControlConfig(enabled, FilterMode::Allowlist, std::vector<std::string>{}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        const auto state = std::make_shared<const PolicyManager::State>(PolicyManager::State{
            SecurityContext(1, 10, 20),
            testCase.hasPolicy ? snapshot : nullptr,
            testCase.hasTimestamp
                ? std::optional<PolicyManager::Clock::time_point>{PolicyManager::Clock::time_point{}}
                : std::nullopt});
        const PolicyManager::PolicyReadResult policy{
            testCase.availability, testCase.hasState ? state : nullptr};
        const DownloadPolicy evaluator;

        const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(testCase.expectedReason, decision.Reason());
    }
}

INSTANTIATE_TEST_SUITE_P(
    DefensiveReads,
    DownloadPolicyReadTest,
    ::testing::Values(
        // Available no acredita contexto si falta el estado que debería acompañar la lectura.
        DownloadPolicyReadCase{"AvailableWithoutState", PolicyAvailability::Available,
            false, true, true, SecurityReason::InvalidContext},
        // Sin estado tampoco hay contexto utilizable, aunque se indique Unavailable.
        DownloadPolicyReadCase{"UnavailableWithoutState", PolicyAvailability::Unavailable,
            false, false, false, SecurityReason::InvalidContext},
        // La ausencia de estado prevalece sobre la disponibilidad declarada como Expired.
        DownloadPolicyReadCase{"ExpiredWithoutState", PolicyAvailability::Expired,
            false, true, true, SecurityReason::InvalidContext},
        // El estado tiene contexto e instante de validación, pero carece del snapshot de política.
        DownloadPolicyReadCase{"AvailableWithoutPolicy", PolicyAvailability::Available,
            true, false, true, SecurityReason::PolicyUnavailable},
        // Un snapshot sin instante de validación no acredita una política utilizable.
        DownloadPolicyReadCase{"AvailableWithoutTimestamp", PolicyAvailability::Available,
            true, true, false, SecurityReason::PolicyUnavailable},
        // Sin snapshot se devuelve PolicyUnavailable antes de considerar la caducidad declarada.
        DownloadPolicyReadCase{"ExpiredWithoutPolicy", PolicyAvailability::Expired,
            true, false, true, SecurityReason::PolicyUnavailable},
        // Sin instante de validación tampoco se utiliza Expired para justificar el bloqueo.
        DownloadPolicyReadCase{"ExpiredWithoutTimestamp", PolicyAvailability::Expired,
            true, true, false, SecurityReason::PolicyUnavailable},
        // Un valor de disponibilidad desconocido se rechaza aunque el estado esté completo.
        DownloadPolicyReadCase{"UnknownAvailability", static_cast<PolicyAvailability>(-1),
            true, true, true, SecurityReason::PolicyUnavailable},
        // Conservar datos completos no permite ignorar que el contexto se ha declarado inválido.
        DownloadPolicyReadCase{"InvalidContextWithCompleteState", PolicyAvailability::InvalidContext,
            true, true, true, SecurityReason::InvalidContext},
        // Los datos conservados no convierten una lectura Unavailable en una autorización.
        DownloadPolicyReadCase{"UnavailableWithCompleteState", PolicyAvailability::Unavailable,
            true, true, true, SecurityReason::PolicyUnavailable}),
    // Cada nombre identifica los datos de su caso; la lambda no captura referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyReadCase>& info) {
        return std::string(info.param.name);
    });

// Combina problemas de distintas etapas para comprobar cuál debe justificar la decisión.
struct DownloadPolicyPriorityCase {
    const char* name;
    PolicyAvailability availability;
    const char* fileName;
    SecurityReason expectedReason;
};

class DownloadPolicyPriorityTest : public ::testing::TestWithParam<DownloadPolicyPriorityCase> {};

// Las lecturas proceden del gestor; la tabla define el primer bloqueo esperado.
TEST_P(DownloadPolicyPriorityTest, PreservesFirstBlockingReason) {
    const DownloadPolicyPriorityCase& testCase = GetParam();
    PolicyManager::Clock::time_point now{};
    // now se utiliza secuencialmente y vive durante toda la vida del gestor.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    if (testCase.availability != PolicyAvailability::Unavailable) {
        const PolicySnapshot snapshot(
            1, 20, "Office policy",
            UrlFilteringConfig(false, FilterMode::Denylist, {}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    }
    if (testCase.availability == PolicyAvailability::Expired) {
        now += std::chrono::minutes{15};
    }

    // Una generación ajena produce InvalidContext sin fabricar una lectura permisiva.
    const SecurityContext requested(
        testCase.availability == PolicyAvailability::InvalidContext
            ? context->Generation() + 1 : context->Generation(),
        context->DeviceId(), context->CompanyId());
    const auto policy = manager.ReadPolicy(requested);
    ASSERT_EQ(testCase.availability, policy.availability);
    const DownloadPolicy evaluator;

    const SecurityDecision decision = evaluator.Evaluate(policy, testCase.fileName);

    EXPECT_EQ(SecurityAction::Block, decision.Action());
    EXPECT_EQ(testCase.expectedReason, decision.Reason());
}

INSTANTIATE_TEST_SUITE_P(
    BlockingOrder,
    DownloadPolicyPriorityTest,
    ::testing::Values(
        // El contexto inválido se rechaza antes de detectar que NUL.exe es un nombre reservado.
        DownloadPolicyPriorityCase{"ContextBeforeInvalidName", PolicyAvailability::InvalidContext,
            "NUL.exe", SecurityReason::InvalidContext},
        // Sin contexto válido no se llega a comprobar que informe carece de extensión.
        DownloadPolicyPriorityCase{"ContextBeforeMissingExtension", PolicyAvailability::InvalidContext,
            "informe", SecurityReason::InvalidContext},
        // El contexto inválido prevalece aunque exe coincidiría con la DENYLIST.
        DownloadPolicyPriorityCase{"ContextBeforeDeniedExtension", PolicyAvailability::InvalidContext,
            "programa.exe", SecurityReason::InvalidContext},
        // La ausencia de política impide evaluar el nombre reservado NUL.exe.
        DownloadPolicyPriorityCase{"UnavailableBeforeInvalidName", PolicyAvailability::Unavailable,
            "NUL.exe", SecurityReason::PolicyUnavailable},
        // Sin una política publicada no se atribuye el bloqueo a una regla de extensión.
        DownloadPolicyPriorityCase{"UnavailableBeforeDeniedExtension", PolicyAvailability::Unavailable,
            "programa.exe", SecurityReason::PolicyUnavailable},
        // Una política caducada bloquea antes de comprobar la validez del nombre.
        DownloadPolicyPriorityCase{"ExpiredBeforeInvalidName", PolicyAvailability::Expired,
            "NUL.exe", SecurityReason::PolicyExpired},
        // La caducidad prevalece sobre la ausencia de extensión del archivo.
        DownloadPolicyPriorityCase{"ExpiredBeforeMissingExtension", PolicyAvailability::Expired,
            "informe", SecurityReason::PolicyExpired},
        // No se utilizan las reglas caducadas para justificar el bloqueo de exe.
        DownloadPolicyPriorityCase{"ExpiredBeforeDeniedExtension", PolicyAvailability::Expired,
            "programa.exe", SecurityReason::PolicyExpired},
        // Con política vigente, el nombre reservado se rechaza antes de la coincidencia con exe.
        DownloadPolicyPriorityCase{"InvalidNameBeforeDenylist", PolicyAvailability::Available,
            "NUL.exe", SecurityReason::InvalidFileName},
        // La ausencia de extensión bloquea antes de consultar una DENYLIST que no coincidiría.
        DownloadPolicyPriorityCase{"MissingExtensionBeforeDenylist", PolicyAvailability::Available,
            "informe", SecurityReason::MissingExtension}),
    // Cada nombre expresa la precedencia que se comprueba; la lambda no captura objetos del test.
    [](const ::testing::TestParamInfo<DownloadPolicyPriorityCase>& info) {
        return std::string(info.param.name);
    });

// Cada entrada contiene un nombre inválido completo, separado de cualquier carpeta de destino.
struct DownloadPolicyInvalidNameCase {
    const char* name;
    std::string fileName;
};

class DownloadPolicyInvalidNameTest
    : public ::testing::TestWithParam<DownloadPolicyInvalidNameCase> {};

// La validez del nombre es obligatoria en ambos modos y estados de activación.
TEST_P(DownloadPolicyInvalidNameTest, BlocksInvalidNameBeforeExtensionRules) {
    const DownloadPolicyInvalidNameCase& testCase = GetParam();
    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // El reloj fijo mantiene vigente la política y aísla el rechazo del nombre.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());

            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(false, FilterMode::Denylist, {}),
                DownloadControlConfig(enabled, mode, {"pdf", "exe"}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            ASSERT_EQ(PolicyAvailability::Available, policy.availability);
            const DownloadPolicy evaluator;

            const SecurityDecision decision = evaluator.Evaluate(policy, testCase.fileName);

            EXPECT_EQ(SecurityAction::Block, decision.Action());
            EXPECT_EQ(SecurityReason::InvalidFileName, decision.Reason());
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    InvalidWindowsNames,
    DownloadPolicyInvalidNameTest,
    ::testing::Values(
        // No se inventa un nombre alternativo cuando la entrada está vacía.
        DownloadPolicyInvalidNameCase{"EmptyName", ""},
        // El punto aislado representa el directorio actual y no un nombre de descarga.
        DownloadPolicyInvalidNameCase{"CurrentDirectory", "."},
        // La entrada .. representa el directorio superior.
        DownloadPolicyInvalidNameCase{"ParentDirectory", ".."},
        // Un punto final se rechaza sin eliminarlo para aceptar el archivo.
        DownloadPolicyInvalidNameCase{"TrailingDot", "informe.pdf."},
        // Un espacio final se rechaza sin recortarlo silenciosamente.
        DownloadPolicyInvalidNameCase{"TrailingSpace", "informe.pdf "},
        // Un nombre formado solo por espacios tampoco se convierte en otro nombre.
        DownloadPolicyInvalidNameCase{"OnlySpaces", "   "},
        // El signo menor que no puede formar parte de un nombre Windows.
        DownloadPolicyInvalidNameCase{"LessThan", "in<forme.pdf"},
        // El signo mayor que se rechaza aunque la extensión aparente sea pdf.
        DownloadPolicyInvalidNameCase{"GreaterThan", "in>forme.pdf"},
        // Los dos puntos no se admiten en ninguna parte del nombre.
        DownloadPolicyInvalidNameCase{"Colon", "in:forme.pdf"},
        // Las comillas dobles son un carácter reservado del nombre.
        DownloadPolicyInvalidNameCase{"DoubleQuote", "in\"forme.pdf"},
        // La barra introduce una ruta y no se interpreta como parte del nombre.
        DownloadPolicyInvalidNameCase{"ForwardSlash", "carpeta/informe.pdf"},
        // La barra inversa también separa componentes de ruta.
        DownloadPolicyInvalidNameCase{"Backslash", "carpeta\\informe.pdf"},
        // La barra vertical se rechaza antes de evaluar las listas.
        DownloadPolicyInvalidNameCase{"Pipe", "in|forme.pdf"},
        // El signo de interrogación no se admite como carácter del nombre.
        DownloadPolicyInvalidNameCase{"QuestionMark", "in?forme.pdf"},
        // El asterisco tampoco se admite como carácter del nombre.
        DownloadPolicyInvalidNameCase{"Asterisk", "in*forme.pdf"},
        // No se permite seleccionar un flujo alternativo de datos mediante dos puntos.
        DownloadPolicyInvalidNameCase{"AlternateDataStream", "informe.pdf:datos"},
        // La sintaxis explícita de flujo de NTFS tampoco se acepta como nombre.
        DownloadPolicyInvalidNameCase{"ExplicitDataStream", "informe.pdf::$DATA"},
        // El evaluador recibe un nombre, por lo que rechaza una ruta absoluta con unidad.
        DownloadPolicyInvalidNameCase{"AbsoluteDrivePath", "C:\\Descargas\\informe.pdf"},
        // Una referencia relativa a una unidad también contiene sintaxis de ruta.
        DownloadPolicyInvalidNameCase{"RelativeDrivePath", "C:informe.pdf"},
        // Una ruta de red no sustituye al nombre separado de la carpeta.
        DownloadPolicyInvalidNameCase{"UncPath", "\\\\servidor\\carpeta\\informe.pdf"},
        // La entrada no puede utilizar el directorio superior para cambiar el destino.
        DownloadPolicyInvalidNameCase{"ParentRelativePath", "..\\informe.pdf"},
        // Empezar por punto no hace válido un nombre que también termina en punto.
        DownloadPolicyInvalidNameCase{"HiddenNameWithTrailingDot", ".env."}),
    // El nombre descriptivo identifica la entrada inválida sin capturar referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyInvalidNameCase>& info) {
        return std::string(info.param.name);
    });

// Cada dispositivo se comprueba con varias formas del nombre y de la extensión.
struct DownloadPolicyReservedNameCase {
    const char* name;
    std::string baseName;
};

class DownloadPolicyReservedNameTest
    : public ::testing::TestWithParam<DownloadPolicyReservedNameCase> {};

// Un dispositivo reservado se rechaza incluso si su extensión estaría permitida por la política.
TEST_P(DownloadPolicyReservedNameTest, BlocksReservedDeviceNameVariants) {
    const DownloadPolicyReservedNameCase& testCase = GetParam();
    std::string lowercaseName = testCase.baseName;
    // Solo cambian las letras ASCII del identificador; los superíndices conservan sus bytes UTF-8.
    for (char& character : lowercaseName) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // El reloj fijo mantiene la política vigente mientras se evalúan las variantes.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(false, FilterMode::Denylist, {}),
                DownloadControlConfig(enabled, mode, {"pdf", "gz"}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            ASSERT_EQ(PolicyAvailability::Available, policy.availability);
            const DownloadPolicy evaluator;

            // Se comprueban mayúsculas y minúsculas mediante entradas independientes del evaluador.
            for (const std::string& baseName : {testCase.baseName, lowercaseName}) {
                // Incluye ausencia de extensión, extensión simple, múltiple y espacio antes del punto.
                for (const char* suffix : {"", ".pdf", ".tar.gz", " .pdf"}) {
                    const std::string fileName = baseName + suffix;
                    SCOPED_TRACE(fileName);

                    const SecurityDecision decision = evaluator.Evaluate(policy, fileName);

                    EXPECT_EQ(SecurityAction::Block, decision.Action());
                    EXPECT_EQ(SecurityReason::InvalidFileName, decision.Reason());
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    ReservedWindowsDevices,
    DownloadPolicyReservedNameTest,
    ::testing::Values(
        // CON identifica la consola y no puede utilizarse como nombre ordinario.
        DownloadPolicyReservedNameCase{"Console", "CON"},
        // PRN es un identificador reservado de impresora.
        DownloadPolicyReservedNameCase{"Printer", "PRN"},
        // AUX es un identificador reservado de dispositivo auxiliar.
        DownloadPolicyReservedNameCase{"Auxiliary", "AUX"},
        // NUL identifica el dispositivo nulo, incluso cuando se añade una extensión.
        DownloadPolicyReservedNameCase{"NullDevice", "NUL"},
        // COM1 es el primer identificador reservado de la serie COM.
        DownloadPolicyReservedNameCase{"Com1", "COM1"},
        // COM2 también pertenece a la serie reservada.
        DownloadPolicyReservedNameCase{"Com2", "COM2"},
        // COM3 se rechaza con las mismas variantes de nombre y extensión.
        DownloadPolicyReservedNameCase{"Com3", "COM3"},
        // COM4 mantiene la reserva independientemente del modo de la política.
        DownloadPolicyReservedNameCase{"Com4", "COM4"},
        // COM5 sigue siendo inválido con Download Control desactivado.
        DownloadPolicyReservedNameCase{"Com5", "COM5"},
        // COM6 no se permite por añadir una extensión válida.
        DownloadPolicyReservedNameCase{"Com6", "COM6"},
        // COM7 tampoco se permite con varias extensiones.
        DownloadPolicyReservedNameCase{"Com7", "COM7"},
        // COM8 se reconoce tanto en mayúsculas como en minúsculas.
        DownloadPolicyReservedNameCase{"Com8", "COM8"},
        // COM9 comprueba el extremo superior de los dígitos ASCII reservados.
        DownloadPolicyReservedNameCase{"Com9", "COM9"},
        // LPT1 es el primer identificador reservado de la serie LPT.
        DownloadPolicyReservedNameCase{"Lpt1", "LPT1"},
        // LPT2 también pertenece a la serie reservada.
        DownloadPolicyReservedNameCase{"Lpt2", "LPT2"},
        // LPT3 se rechaza con las mismas variantes de nombre y extensión.
        DownloadPolicyReservedNameCase{"Lpt3", "LPT3"},
        // LPT4 mantiene la reserva independientemente del modo de la política.
        DownloadPolicyReservedNameCase{"Lpt4", "LPT4"},
        // LPT5 sigue siendo inválido con Download Control desactivado.
        DownloadPolicyReservedNameCase{"Lpt5", "LPT5"},
        // LPT6 no se permite por añadir una extensión válida.
        DownloadPolicyReservedNameCase{"Lpt6", "LPT6"},
        // LPT7 tampoco se permite con varias extensiones.
        DownloadPolicyReservedNameCase{"Lpt7", "LPT7"},
        // LPT8 se reconoce tanto en mayúsculas como en minúsculas.
        DownloadPolicyReservedNameCase{"Lpt8", "LPT8"},
        // LPT9 comprueba el extremo superior de los dígitos ASCII reservados.
        DownloadPolicyReservedNameCase{"Lpt9", "LPT9"},
        // Windows también reserva COM seguido del superíndice ¹, expresado en bytes UTF-8.
        DownloadPolicyReservedNameCase{"ComSuperscriptOne", "COM\xC2\xB9"},
        // El superíndice ² pertenece a las variantes reservadas de COM.
        DownloadPolicyReservedNameCase{"ComSuperscriptTwo", "COM\xC2\xB2"},
        // El superíndice ³ completa las variantes reservadas de COM.
        DownloadPolicyReservedNameCase{"ComSuperscriptThree", "COM\xC2\xB3"},
        // LPT seguido del superíndice ¹ también es un nombre reservado de Windows.
        DownloadPolicyReservedNameCase{"LptSuperscriptOne", "LPT\xC2\xB9"},
        // El superíndice ² pertenece a las variantes reservadas de LPT.
        DownloadPolicyReservedNameCase{"LptSuperscriptTwo", "LPT\xC2\xB2"},
        // El superíndice ³ completa las variantes reservadas de LPT.
        DownloadPolicyReservedNameCase{"LptSuperscriptThree", "LPT\xC2\xB3"}),
    // El nombre identifica el dispositivo base; la lambda no conserva referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyReservedNameCase>& info) {
        return std::string(info.param.name);
    });

// Reutiliza la prueba de nombres inválidos: cambia la entrada, pero mantiene modo y activación.
INSTANTIATE_TEST_SUITE_P(
    MalformedUtf8Names,
    DownloadPolicyInvalidNameTest,
    ::testing::Values(
        // Un byte de continuación no puede iniciar por sí solo un carácter UTF-8.
        DownloadPolicyInvalidNameCase{"UnexpectedContinuation", "informe.\x80"},
        // El inicio de una secuencia de dos bytes necesita un byte de continuación.
        DownloadPolicyInvalidNameCase{"TruncatedTwoByteSequence", "informe.pdf\xC3"},
        // Esta secuencia de tres bytes termina antes de recibir el último.
        DownloadPolicyInvalidNameCase{"TruncatedThreeByteSequence", "informe.pdf\xE2\x82"},
        // Esta secuencia de cuatro bytes también queda incompleta al final del nombre.
        DownloadPolicyInvalidNameCase{"TruncatedFourByteSequence", "informe.pdf\xF0\x9F\x98"},
        // Un paréntesis ASCII no puede completar una secuencia iniciada por C3.
        DownloadPolicyInvalidNameCase{"InvalidTwoByteContinuation", "informe.\xC3\x28"},
        // El segundo byte de una secuencia de tres bytes debe ser de continuación.
        DownloadPolicyInvalidNameCase{"InvalidThreeByteMiddle", "informe.\xE2\x28\xA1"},
        // También se comprueba el último byte de una secuencia de tres bytes.
        DownloadPolicyInvalidNameCase{"InvalidThreeByteLast", "informe.\xE2\x82\x28"},
        // El segundo byte de una secuencia de cuatro bytes no puede ser ASCII.
        DownloadPolicyInvalidNameCase{"InvalidFourByteSecond", "informe.\xF0\x28\x8C\xBC"},
        // El tercer byte de una secuencia de cuatro bytes también debe continuar el carácter.
        DownloadPolicyInvalidNameCase{"InvalidFourByteThird", "informe.\xF0\x90\x28\x80"},
        // El cuarto byte tampoco puede sustituirse por un carácter ASCII.
        DownloadPolicyInvalidNameCase{"InvalidFourByteLast", "informe.\xF0\x90\x80\x28"},
        // No se admite representar un carácter ASCII mediante una secuencia de dos bytes.
        DownloadPolicyInvalidNameCase{"OverlongTwoByteSequence", "informe.\xC0\xAF"},
        // La misma codificación demasiado larga se rechaza cuando utiliza tres bytes.
        DownloadPolicyInvalidNameCase{"OverlongThreeByteSequence", "informe.\xE0\x80\xAF"},
        // Utilizar cuatro bytes tampoco permite ocultar una representación demasiado larga.
        DownloadPolicyInvalidNameCase{"OverlongFourByteSequence", "informe.\xF0\x80\x80\xAF"},
        // U+D800 es un sustituto UTF-16 y no un valor escalar Unicode válido.
        DownloadPolicyInvalidNameCase{"Utf16HighSurrogate", "informe.\xED\xA0\x80"},
        // U+DFFF tampoco puede aparecer como un carácter UTF-8 válido.
        DownloadPolicyInvalidNameCase{"Utf16LowSurrogate", "informe.\xED\xBF\xBF"},
        // La secuencia representa U+110000, por encima del máximo Unicode U+10FFFF.
        DownloadPolicyInvalidNameCase{"BeyondUnicodeMaximum", "informe.\xF4\x90\x80\x80"},
        // F5 no puede iniciar una secuencia UTF-8 válida.
        DownloadPolicyInvalidNameCase{"InvalidLeadingF5", "informe.\xF5\x80\x80\x80"},
        // Las antiguas secuencias de cinco bytes no forman parte del UTF-8 válido.
        DownloadPolicyInvalidNameCase{"ObsoleteFiveByteSequence", "informe.\xF8\x88\x80\x80\x80"},
        // Las antiguas secuencias de seis bytes tampoco se admiten.
        DownloadPolicyInvalidNameCase{"ObsoleteSixByteSequence", "informe.\xFC\x84\x80\x80\x80\x80"},
        // FE es un byte inválido en cualquier posición de una secuencia UTF-8.
        DownloadPolicyInvalidNameCase{"InvalidByteFE", "informe.\xFE"},
        // FF también se rechaza sin sustituirlo por otro carácter.
        DownloadPolicyInvalidNameCase{"InvalidByteFF", "informe.\xFF"}),
    // El nombre identifica la secuencia malformada; la lambda no captura referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyInvalidNameCase>& info) {
        return std::string(info.param.name);
    });

// Divide los controles en rangos para que CTest registre cada grupo por separado.
struct DownloadPolicyControlRangeCase {
    const char* name;
    char32_t first;
    char32_t last;
};

class DownloadPolicyControlTest
    : public ::testing::TestWithParam<DownloadPolicyControlRangeCase> {};

// Se prueban todos los controles del rango al inicio, en medio y al final del nombre.
TEST_P(DownloadPolicyControlTest, BlocksEveryControlInFileName) {
    const DownloadPolicyControlRangeCase& testCase = GetParam();
    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // El reloj fijo evita que el estado de la política cambie durante el recorrido.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(false, FilterMode::Denylist, {}),
                DownloadControlConfig(enabled, mode, {"pdf"}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            ASSERT_EQ(PolicyAvailability::Available, policy.availability);
            const DownloadPolicy evaluator;

            for (char32_t codePoint = testCase.first; codePoint <= testCase.last; ++codePoint) {
                SCOPED_TRACE(static_cast<unsigned int>(codePoint));
                std::string control;
                // C0 y DEL ocupan un byte; los controles C1 utilizan C2 y un byte de continuación.
                if (codePoint > 0x7F) {
                    control.push_back('\xC2');
                }
                control.push_back(static_cast<char>(codePoint));

                // Las cadenas conservan su longitud, también cuando el control es NUL.
                for (const std::string& fileName : {
                    control + "informe.pdf", "in" + control + "forme.pdf", "informe.pdf" + control}) {
                    const SecurityDecision decision = evaluator.Evaluate(policy, fileName);

                    EXPECT_EQ(SecurityAction::Block, decision.Action());
                    EXPECT_EQ(SecurityReason::InvalidFileName, decision.Reason());
                }
            }
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    IsoControlRanges,
    DownloadPolicyControlTest,
    ::testing::Values(
        // Incluye los 32 controles C0, entre ellos NUL, tabulador y saltos de línea.
        DownloadPolicyControlRangeCase{"C0", 0x00, 0x1F},
        // DEL es un control aunque esté fuera del intervalo C0 anterior.
        DownloadPolicyControlRangeCase{"Delete", 0x7F, 0x7F},
        // Incluye los 32 controles C1 como UTF-8 válido, para aislar el rechazo del control.
        DownloadPolicyControlRangeCase{"C1", 0x80, 0x9F}),
    // El nombre distingue el rango de controles; la lambda no captura referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyControlRangeCase>& info) {
        return std::string(info.param.name);
    });

// Los motivos esperados se fijan en la tabla, sin extraer ni normalizar extensiones en el test.
struct DownloadPolicyExtensionCase {
    const char* name;
    std::string fileName;
    std::vector<std::string> rules;
    SecurityReason denylistReason;
    SecurityReason allowlistReason;
};

class DownloadPolicyExtensionTest
    : public ::testing::TestWithParam<DownloadPolicyExtensionCase> {};

// Cada nombre válido se comprueba con ambos modos y con el control activado o desactivado.
TEST_P(DownloadPolicyExtensionTest, AppliesLastExtensionRulesAndActivation) {
    const DownloadPolicyExtensionCase& testCase = GetParam();
    for (const FilterMode mode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(mode == FilterMode::Denylist ? "denylist" : "allowlist");
        for (const bool enabled : {false, true}) {
            SCOPED_TRACE(enabled ? "enabled" : "disabled");
            // El reloj fijo mantiene la política vigente durante cada combinación de la tabla.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(false, FilterMode::Denylist, {}),
                DownloadControlConfig(enabled, mode, testCase.rules),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            ASSERT_EQ(PolicyAvailability::Available, policy.availability);
            const DownloadPolicy evaluator;

            // La desactivación omite listas y ausencia de extensión; el nombre sigue siendo válido.
            const SecurityReason expectedReason = !enabled ? SecurityReason::Allowed
                : (mode == FilterMode::Denylist ? testCase.denylistReason : testCase.allowlistReason);
            const SecurityDecision decision = evaluator.Evaluate(policy, testCase.fileName);

            EXPECT_EQ(expectedReason == SecurityReason::Allowed
                ? SecurityAction::Allow : SecurityAction::Block, decision.Action());
            EXPECT_EQ(expectedReason, decision.Reason());
        }
    }
}

INSTANTIATE_TEST_SUITE_P(
    LastExtensionsAndLists,
    DownloadPolicyExtensionTest,
    ::testing::Values(
        // Una extensión pdf en minúsculas coincide exactamente con su regla.
        DownloadPolicyExtensionCase{"LowercasePdf", "informe.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Las mayúsculas de la extensión no evitan la coincidencia con pdf.
        DownloadPolicyExtensionCase{"UppercasePdf", "informe.PDF", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El snapshot puede conservar una regla en mayúsculas que se normaliza al comparar.
        DownloadPolicyExtensionCase{"UppercaseRule", "informe.pdf", {"PDF"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El punto inicial opcional de la regla no forma parte de la extensión comparada.
        DownloadPolicyExtensionCase{"LeadingDotRule", "informe.pdf", {".PDF"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Una extensión EXE se compara como exe y produce el resultado propio de cada modo.
        DownloadPolicyExtensionCase{"UppercaseExe", "programa.EXE", {"exe"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // En una doble extensión gobierna exe, que es la última.
        DownloadPolicyExtensionCase{"DoubleExtensionLastMatches", "documento.pdf.exe", {"exe"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La presencia anterior de pdf no hace coincidir un archivo cuya extensión final es exe.
        DownloadPolicyExtensionCase{"EarlierExtensionIgnored", "documento.pdf.exe", {"pdf"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Cambiar el orden de las extensiones también cambia la que gobierna la evaluación.
        DownloadPolicyExtensionCase{"ReversedDoubleExtension", "documento.exe.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Con varios puntos solo se compara la extensión final gz.
        DownloadPolicyExtensionCase{"MultipleExtensions", "archivo.backup.tar.gz", {"gz"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La regla pdf no coincide parcialmente con pdfx.
        DownloadPolicyExtensionCase{"PartialExtensionDoesNotMatch", "informe.pdfx", {"pdf"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Una lista vacía permite por DENYLIST y bloquea por ALLOWLIST cuando hay extensión.
        DownloadPolicyExtensionCase{"EmptyRules", "informe.pdf", {},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Una regla distinta produce una no coincidencia, aunque la lista no esté vacía.
        DownloadPolicyExtensionCase{"DifferentRule", "informe.pdf", {"txt"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // La búsqueda continúa si la regla que coincide no es la primera.
        DownloadPolicyExtensionCase{"LaterRuleMatches", "informe.pdf", {"txt", "pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Sin punto no hay extensión, con independencia del modo.
        DownloadPolicyExtensionCase{"MissingExtension", "informe", {"pdf"},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension},
        // El único punto de .env es inicial y no introduce una extensión.
        DownloadPolicyExtensionCase{"DotEnv", ".env", {"env"},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension},
        // .gitignore sigue el mismo contrato de ausencia de extensión.
        DownloadPolicyExtensionCase{"DotGitignore", ".gitignore", {"gitignore"},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension},
        // Un nombre que empieza por punto sí tiene extensión si aparece otro punto después.
        DownloadPolicyExtensionCase{"HiddenJson", ".config.json", {"json"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Empezar por punto no oculta la extensión final de un nombre con varias extensiones.
        DownloadPolicyExtensionCase{"HiddenDoubleExtension", ".documento.pdf.exe", {"exe"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Dos puntos interiores consecutivos no invalidan por sí solos un nombre Windows.
        DownloadPolicyExtensionCase{"RepeatedInteriorDot", "informe..pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Los espacios interiores del nombre base se conservan y no impiden evaluar pdf.
        DownloadPolicyExtensionCase{"SpacesInBaseName", "informe final.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // COM0 queda fuera de la lista de identificadores reservados COM1 a COM9.
        DownloadPolicyExtensionCase{"ComZeroIsNotReserved", "COM0.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // COM10 tampoco debe confundirse con el identificador reservado COM1.
        DownloadPolicyExtensionCase{"ComTenIsNotReserved", "COM10.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // LPT0 queda fuera de los identificadores reservados LPT1 a LPT9.
        DownloadPolicyExtensionCase{"LptZeroIsNotReserved", "LPT0.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La comparación de nombres reservados no puede bloquear por el prefijo LPT1.
        DownloadPolicyExtensionCase{"LptTenIsNotReserved", "LPT10.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Un nombre ordinario que empieza por NUL no es el dispositivo NUL.
        DownloadPolicyExtensionCase{"ReservedPrefixInOrdinaryName", "NULdata.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El punto inicial hace que .NUL.pdf sea distinto del dispositivo NUL.
        DownloadPolicyExtensionCase{"LeadingDotBeforeReservedText", ".NUL.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La palabra auxiliary no se confunde con el identificador reservado AUX.
        DownloadPolicyExtensionCase{"AuxiliaryIsOrdinaryName", "auxiliary.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Cambiar las mayúsculas del nombre base no cambia la extensión final que se evalúa.
        DownloadPolicyExtensionCase{"MixedCaseBaseName", "MiInforme.PdF", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La ausencia de extensión bloquea incluso cuando la DENYLIST está vacía.
        DownloadPolicyExtensionCase{"MissingExtensionWithEmptyRules", "informe", {},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension},
        // Una lista vacía tampoco convierte .env en un archivo con extensión.
        DownloadPolicyExtensionCase{"DotEnvWithEmptyRules", ".env", {},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension}),
    // Cada nombre identifica los datos y resultados de su fila; no se capturan referencias externas.
    [](const ::testing::TestParamInfo<DownloadPolicyExtensionCase>& info) {
        return std::string(info.param.name);
    });

// Los bytes UTF-8 explícitos evitan depender de la página de códigos del compilador.
// Los casos especiales conservan los criterios Unicode ya comprobados en PolicyManager.
INSTANTIATE_TEST_SUITE_P(
    UnicodeExtensionsAndNames,
    DownloadPolicyExtensionTest,
    ::testing::Values(
        // La ñ del nombre base se conserva y no interfiere con la extensión pdf.
        DownloadPolicyExtensionCase{"EnyeInBaseName", "ni\xC3\xB1" "o.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Un carácter japonés de tres bytes es válido dentro del nombre base.
        DownloadPolicyExtensionCase{"ThreeByteBaseName", "informe_\xE6\x97\xA5.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Un emoji de cuatro bytes también puede formar parte de un nombre válido.
        DownloadPolicyExtensionCase{"EmojiInBaseName", "informe_\xF0\x9F\x98\x80.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Ñ se convierte en ñ al preparar la extensión para comparar.
        DownloadPolicyExtensionCase{"UppercaseEnyeExtension", "informe.\xC3\x91", {"\xC3\xB1"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La regla también se convierte a minúsculas y admite un punto inicial.
        DownloadPolicyExtensionCase{"UppercaseEnyeRule", "informe.\xC3\xB1", {".\xC3\x91"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Convertir a minúsculas no elimina el acento: é sigue siendo distinta de e.
        DownloadPolicyExtensionCase{"AccentsRemainDistinct", "informe.\xC3\xA9", {"e"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // No se aplica normalización canónica para unificar é y e con acento combinado.
        DownloadPolicyExtensionCase{"ComposedAndDecomposedRemainDistinct",
            "informe.\xC3\xA9", {"e\xCC\x81"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Una secuencia con acento combinado sí coincide con la misma secuencia en la regla.
        DownloadPolicyExtensionCase{"CombiningSequenceMatches",
            "informe.e\xCC\x81", {"e\xCC\x81"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El locale raíz convierte I en i sin depender del idioma de Windows.
        DownloadPolicyExtensionCase{"RootLocaleAsciiI", "informe.I", {"i"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // İ se convierte en i seguida de U+0307, por lo que la conversión cambia de longitud.
        DownloadPolicyExtensionCase{"RootLocaleDottedIExpansion",
            "informe.\xC4\xB0", {"i\xCC\x87"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La i con punto combinado no se reduce silenciosamente a una i simple.
        DownloadPolicyExtensionCase{"DottedIAndPlainIRemainDistinct", "informe.\xC4\xB0", {"i"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // La i sin punto U+0131 tampoco se equipara a la i ASCII.
        DownloadPolicyExtensionCase{"DotlessIAndPlainIRemainDistinct", "informe.\xC4\xB1", {"i"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // ΟΣ pasa a ος: la conversión de sigma mayúscula considera su posición en la extensión.
        DownloadPolicyExtensionCase{"GreekFinalSigma",
            "informe.\xCE\x9F\xCE\xA3", {"\xCE\xBF\xCF\x82"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Las formas ya minúsculas οσ y ος se conservan distintas; no se aplica case folding.
        DownloadPolicyExtensionCase{"GreekLowercaseSigmasRemainDistinct",
            "informe.\xCE\xBF\xCF\x83", {"\xCE\xBF\xCF\x82"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // ß no se convierte en ss mediante esta conversión a minúsculas.
        DownloadPolicyExtensionCase{"SharpSAndSsRemainDistinct", "informe.\xC3\x9F", {"ss"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // La letra cirílica р y la letra latina p son distintas aunque se parezcan visualmente.
        DownloadPolicyExtensionCase{"CyrillicAndLatinRemainDistinct", "informe.\xD1\x80", {"p"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Las letras de ancho completo ｐｄｆ no se convierten en las letras ASCII pdf.
        DownloadPolicyExtensionCase{"FullwidthLettersRemainDistinct",
            "informe.\xEF\xBD\x90\xEF\xBD\x84\xEF\xBD\x86", {"pdf"},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // Una extensión con emoji se compara exactamente; las reglas no se limitan a ASCII.
        DownloadPolicyExtensionCase{"EmojiExtensionMatches",
            "informe.\xF0\x9F\x98\x80", {"\xF0\x9F\x98\x80"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // Una regla de 20 puntos de código puede ocupar más de 20 bytes UTF-8.
        DownloadPolicyExtensionCase{"MultibyteRuleAtCodePointLimit",
            "informe." + std::string(19, 'a') + "\xC3\xB1", {std::string(19, 'a') + "\xC3\xB1"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El límite de longitud de las reglas no se utiliza para invalidar la extensión del archivo.
        DownloadPolicyExtensionCase{"LongFileExtensionDoesNotMatchShorterRule",
            "informe." + std::string(21, 'a'), {std::string(20, 'a')},
            SecurityReason::Allowed, SecurityReason::ExtensionNotAllowed},
        // É se convierte en é sin perder el acento y coincide con una regla con punto inicial.
        DownloadPolicyExtensionCase{"UppercaseAccentedExtension",
            "informe.\xC3\x89", {".\xC3\xA9"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // La búsqueda también encuentra una coincidencia Unicode después de una regla distinta.
        DownloadPolicyExtensionCase{"LaterUnicodeRuleMatches",
            "informe.\xC3\x91", {"pdf", "\xC3\xB1"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed},
        // El punto de ancho completo no sustituye al separador ASCII de extensión.
        DownloadPolicyExtensionCase{"FullwidthDotIsNotExtensionSeparator",
            "informe\xEF\xBC\x8E" "pdf", {"pdf"},
            SecurityReason::MissingExtension, SecurityReason::MissingExtension},
        // Un espacio no separable dentro del nombre base se conserva como Unicode válido.
        DownloadPolicyExtensionCase{"UnicodeSpaceInsideBaseName",
            "informe\xC2\xA0" "final.pdf", {"pdf"},
            SecurityReason::ExtensionDenied, SecurityReason::Allowed}),
    // El nombre distingue el contrato Unicode de cada fila; la lambda no captura referencias.
    [](const ::testing::TestParamInfo<DownloadPolicyExtensionCase>& info) {
        return std::string(info.param.name);
    });

// La lectura incoherente solo pertenece al test: PolicyManager rechaza estos modos al publicar.
TEST(DownloadPolicyTest, UnknownModeNeverAllowsDownload) {
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        const auto snapshot = std::make_shared<const PolicySnapshot>(
            1, 20, "Invalid policy",
            UrlFilteringConfig(false, FilterMode::Denylist, std::vector<std::string>{}),
            DownloadControlConfig(enabled, static_cast<FilterMode>(-1), std::vector<std::string>{}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        const auto state = std::make_shared<const PolicyManager::State>(PolicyManager::State{
            SecurityContext(1, 10, 20), snapshot, PolicyManager::Clock::time_point{}});
        const PolicyManager::PolicyReadResult policy{PolicyAvailability::Available, state};
        const DownloadPolicy evaluator;

        const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
    }
}

// Rechazar la primera política mantiene el bloqueo, incluso con Download Control desactivado.
TEST(DownloadPolicyTest, RejectedInitialPolicyRemainsUnavailable) {
    for (const bool enabled : {false, true}) {
        SCOPED_TRACE(enabled ? "enabled" : "disabled");
        // El reloj fijo permite comprobar únicamente el rechazo de la política inicial.
        PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
        const auto context = manager.BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());
        const PolicySnapshot invalid(
            1, 20, "Invalid policy",
            UrlFilteringConfig(false, FilterMode::Denylist, {}),
            DownloadControlConfig(enabled, FilterMode::Allowlist, {"tar.gz"}),
            PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
        ASSERT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*context, invalid));
        const auto policy = manager.ReadPolicy(*context);
        ASSERT_EQ(PolicyAvailability::Unavailable, policy.availability);
        const DownloadPolicy evaluator;

        const SecurityDecision decision = evaluator.Evaluate(policy, "informe.pdf");

        EXPECT_EQ(SecurityAction::Block, decision.Action());
        EXPECT_EQ(SecurityReason::PolicyUnavailable, decision.Reason());
    }
}

// Una actualización rechazada conserva las reglas anteriores y no reinicia su vigencia.
TEST(DownloadPolicyTest, RejectedReplacementPreservesDecisionAndExpiry) {
    PolicyManager::Clock::time_point now{};
    // now vive más que el gestor y se modifica únicamente desde el hilo de la prueba.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot original(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, original));
    const auto originalState = manager.ReadState();
    ASSERT_TRUE(originalState);

    now += std::chrono::minutes{14};
    const PolicySnapshot invalid(
        2, 20, "Invalid replacement",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(false, FilterMode::Allowlist, {"tar.gz"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*context, invalid));
    EXPECT_EQ(originalState, manager.ReadState());
    const DownloadPolicy evaluator;
    const auto beforeExpiry = evaluator.Evaluate(manager.ReadPolicy(*context), "programa.exe");
    EXPECT_EQ(SecurityAction::Block, beforeExpiry.Action());
    EXPECT_EQ(SecurityReason::ExtensionDenied, beforeExpiry.Reason());

    now += std::chrono::minutes{1};
    const auto expired = evaluator.Evaluate(manager.ReadPolicy(*context), "programa.exe");
    EXPECT_EQ(SecurityAction::Block, expired.Action());
    EXPECT_EQ(SecurityReason::PolicyExpired, expired.Reason());
}

// Una lectura nueva utiliza las reglas sustituidas aunque el ID y las fechas sigan iguales.
TEST(DownloadPolicyTest, FreshReadUsesReplacementRules) {
    // Ambas publicaciones permanecen vigentes para aislar el cambio de configuración.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot original(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, original));
    const DownloadPolicy evaluator;
    const auto before = evaluator.Evaluate(manager.ReadPolicy(*context), "programa.exe");
    EXPECT_EQ(SecurityAction::Block, before.Action());
    EXPECT_EQ(SecurityReason::ExtensionDenied, before.Reason());

    const PolicySnapshot replacement(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Denylist, {"pdf"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, replacement));
    const auto current = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, current.availability);
    const auto allowed = evaluator.Evaluate(current, "programa.exe");
    EXPECT_EQ(SecurityAction::Allow, allowed.Action());
    EXPECT_EQ(SecurityReason::Allowed, allowed.Reason());
    const auto blocked = evaluator.Evaluate(current, "informe.pdf");
    EXPECT_EQ(SecurityAction::Block, blocked.Action());
    EXPECT_EQ(SecurityReason::ExtensionDenied, blocked.Reason());
}

// La referencia conservada mantiene su snapshot inmutable después de publicar una sustitución.
// La autorización real requiere una lectura reciente y la comprobación final de contexto.
TEST(DownloadPolicyTest, RetainedReadPreservesSnapshotAfterReplacement) {
    // El tiempo fijo permite comprobar la vida de las referencias sin introducir caducidad.
    PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot original(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Allowlist, {"pdf"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, original));
    const auto retained = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, retained.availability);
    ASSERT_TRUE(retained.state);
    ASSERT_TRUE(retained.state->policy);

    const PolicySnapshot replacement(
        2, 20, "Updated policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Allowlist, {"exe"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, replacement));
    const auto current = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, current.availability);
    ASSERT_TRUE(current.state);
    ASSERT_TRUE(current.state->policy);
    EXPECT_NE(retained.state, current.state);
    EXPECT_EQ((std::vector<std::string>{"pdf"}), retained.state->policy->DownloadControl().Rules());
    EXPECT_EQ((std::vector<std::string>{"exe"}), current.state->policy->DownloadControl().Rules());

    const DownloadPolicy evaluator;
    const auto previousDecision = evaluator.Evaluate(retained, "informe.pdf");
    EXPECT_EQ(SecurityAction::Allow, previousDecision.Action());
    EXPECT_EQ(SecurityReason::Allowed, previousDecision.Reason());
    const auto currentDecision = evaluator.Evaluate(current, "informe.pdf");
    EXPECT_EQ(SecurityAction::Block, currentDecision.Action());
    EXPECT_EQ(SecurityReason::ExtensionNotAllowed, currentDecision.Reason());
}

// Repetir una evaluación conserva el nombre, las reglas y el estado y no consulta el reloj.
TEST(DownloadPolicyTest, RepeatedEvaluationPreservesInputAndStateAndDoesNotReadClock) {
    int clockReads = 0;
    // El contador vive más que el gestor y solo se incrementa secuencialmente en este hilo.
    PolicyManager manager([&clockReads] {
        ++clockReads;
        return PolicyManager::Clock::time_point{};
    });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const std::vector<std::string> originalRules{".\xC3\x91", "EXE"};
    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Allowlist, originalRules),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    const auto policy = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, policy.availability);
    ASSERT_TRUE(policy.state);
    ASSERT_TRUE(policy.state->policy);
    const int readsBeforeEvaluation = clockReads;
    const auto validatedAt = policy.state->validatedAt;
    const std::string originalName = "informe.\xC3\x91";
    std::string fileName = originalName;
    const DownloadPolicy evaluator;

    for (int index = 0; index < 5; ++index) {
        SCOPED_TRACE(index);
        const auto decision = evaluator.Evaluate(policy, fileName);
        EXPECT_EQ(SecurityAction::Allow, decision.Action());
        EXPECT_EQ(SecurityReason::Allowed, decision.Reason());
    }

    EXPECT_EQ(readsBeforeEvaluation, clockReads);
    EXPECT_EQ(originalName, fileName);
    EXPECT_EQ(policy.state, manager.ReadState());
    EXPECT_EQ(validatedAt, policy.state->validatedAt);
    EXPECT_EQ(originalRules, policy.state->policy->DownloadControl().Rules());
    EXPECT_TRUE(policy.state->policy->DownloadControl().Enabled());
    EXPECT_EQ(FilterMode::Allowlist, policy.state->policy->DownloadControl().Mode());
}

// Permitir una evaluación a los catorce minutos no concede otros quince minutos de vigencia.
TEST(DownloadPolicyTest, EvaluationDoesNotRenewPolicy) {
    PolicyManager::Clock::time_point now{};
    // now se controla en el hilo del test y permanece vivo durante el uso del gestor.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot snapshot(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Allowlist, {"pdf"}),
        PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
    const DownloadPolicy evaluator;
    now += std::chrono::minutes{14};
    const auto allowed = evaluator.Evaluate(manager.ReadPolicy(*context), "informe.pdf");
    EXPECT_EQ(SecurityAction::Allow, allowed.Action());
    EXPECT_EQ(SecurityReason::Allowed, allowed.Reason());

    now += std::chrono::minutes{1};
    const auto expired = evaluator.Evaluate(manager.ReadPolicy(*context), "informe.pdf");
    EXPECT_EQ(SecurityAction::Block, expired.Action());
    EXPECT_EQ(SecurityReason::PolicyExpired, expired.Reason());
}

// La activación y el modo de URL Filtering no cambian las reglas de Download Control.
TEST(DownloadPolicyTest, DownloadRulesAreIndependentOfUrlFiltering) {
    for (const FilterMode urlMode : {FilterMode::Denylist, FilterMode::Allowlist}) {
        SCOPED_TRACE(urlMode == FilterMode::Denylist ? "URL denylist" : "URL allowlist");
        for (const bool urlEnabled : {false, true}) {
            SCOPED_TRACE(urlEnabled ? "URL enabled" : "URL disabled");
            // El reloj fijo mantiene vigente cada política y aísla ambas configuraciones.
            PolicyManager manager([] { return PolicyManager::Clock::time_point{}; });
            const auto context = manager.BeginContext(10, 20);
            ASSERT_TRUE(context.has_value());
            const PolicySnapshot snapshot(
                1, 20, "Office policy",
                UrlFilteringConfig(urlEnabled, urlMode, {"example.com"}),
                DownloadControlConfig(true, FilterMode::Allowlist, {"pdf"}),
                PolicySnapshot::Timestamp{}, PolicySnapshot::Timestamp{});
            ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, snapshot));
            const auto policy = manager.ReadPolicy(*context);
            ASSERT_EQ(PolicyAvailability::Available, policy.availability);
            const DownloadPolicy evaluator;

            const auto allowed = evaluator.Evaluate(policy, "informe.pdf");
            EXPECT_EQ(SecurityAction::Allow, allowed.Action());
            EXPECT_EQ(SecurityReason::Allowed, allowed.Reason());
            const auto blocked = evaluator.Evaluate(policy, "programa.exe");
            EXPECT_EQ(SecurityAction::Block, blocked.Action());
            EXPECT_EQ(SecurityReason::ExtensionNotAllowed, blocked.Reason());
        }
    }
}

}  // namespace sbp
