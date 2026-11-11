#include "security/policy_manager.h"

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <latch>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sbp {

// Una política completa de la compañía esperada supera todas las etapas de validación.
TEST(PolicyManagerTest, ValidPolicyHasNoValidationError) {
    const PolicySnapshot::Timestamp createdAt{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot::Timestamp updatedAt{std::chrono::microseconds{1'700'000'001'654'321}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        createdAt, updatedAt);
    const PolicyManager manager;

    EXPECT_EQ(PolicyValidationError::None, manager.Validate(policy, 2));
}

// Agrupa los identificadores de entrada y el resultado esperado de cada caso.
struct PolicyManagerIdentityCase {
    std::int64_t policyId;
    std::int64_t companyId;
    std::int64_t expectedCompanyId;
    PolicyValidationError expectedError;
};

// Ejecuta la misma comprobación con cada combinación de identidad inválida.
class PolicyManagerIdentityTest : public ::testing::TestWithParam<PolicyManagerIdentityCase> {};

// Mantiene válidos los demás campos para aislar el motivo de rechazo de la identidad.
TEST_P(PolicyManagerIdentityTest, RejectsInvalidOrForeignIdentity) {
    const PolicyManagerIdentityCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        testCase.policyId, testCase.companyId, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(testCase.expectedError, manager.Validate(policy, testCase.expectedCompanyId));
}

// Cubre los límites no positivos de ambos identificadores y una compañía ajena.
INSTANTIATE_TEST_SUITE_P(
    InvalidIdentities,
    PolicyManagerIdentityTest,
    ::testing::Values(
        PolicyManagerIdentityCase{0, 2, 2, PolicyValidationError::InvalidPolicyId},
        PolicyManagerIdentityCase{-1, 2, 2, PolicyValidationError::InvalidPolicyId},
        PolicyManagerIdentityCase{1, 0, 2, PolicyValidationError::InvalidCompanyId},
        PolicyManagerIdentityCase{1, -1, 2, PolicyValidationError::InvalidCompanyId},
        PolicyManagerIdentityCase{1, 3, 2, PolicyValidationError::CompanyMismatch}));

// Identifica el caso, el nombre que se valida y el resultado esperado.
struct PolicyManagerNameCase {
    const char* label;
    std::string name;
    PolicyValidationError expectedError;
};

// Reutiliza la misma política válida cambiando únicamente su nombre.
class PolicyManagerNameTest : public ::testing::TestWithParam<PolicyManagerNameCase> {};

// Comprueba el nombre mediante la entrada pública, manteniendo válidos los demás campos.
TEST_P(PolicyManagerNameTest, ValidatesNameAccordingToBackendContract) {
    const PolicyManagerNameCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, testCase.name,
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(testCase.expectedError, manager.Validate(policy, 2));
}

// Los escapes representan bytes UTF-8 explícitos, independientes de la codificación del archivo fuente.
INSTANTIATE_TEST_SUITE_P(
    PolicyNames,
    PolicyManagerNameTest,
    ::testing::Values(
        PolicyManagerNameCase{"Empty", "", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"AsciiWhitespaceOnly", " \t\r\n", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"UnicodeWhitespaceOnly", "\xE3\x80\x80", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"InternalSpace", "hola mundo", PolicyValidationError::None},
        PolicyManagerNameCase{"AsciiAtLimit", std::string(150, 'a'), PolicyValidationError::None},
        PolicyManagerNameCase{"AsciiBeyondLimit", std::string(151, 'a'), PolicyValidationError::InvalidName},
        PolicyManagerNameCase{
            "EdgeWhitespaceAtLimit", " \t" + std::string(150, 'a') + "\xE3\x80\x80\r\n",
            PolicyValidationError::None},
        PolicyManagerNameCase{
            "InternalSpacesCountTowardsLimit", "a" + std::string(149, ' ') + "b",
            PolicyValidationError::InvalidName},
        PolicyManagerNameCase{
            "TwoByteCodePointAtLimit", std::string(149, 'a') + "\xC3\xA9",
            PolicyValidationError::None},
        PolicyManagerNameCase{
            "ThreeByteCodePointAtLimit", std::string(149, 'a') + "\xE2\x82\xAC",
            PolicyValidationError::None},
        PolicyManagerNameCase{
            "FourByteCodePointAtLimit", std::string(149, 'a') + "\xF0\x9F\x98\x80",
            PolicyValidationError::None},
        PolicyManagerNameCase{
            "FourByteCodePointBeyondLimit", std::string(150, 'a') + "\xF0\x9F\x98\x80",
            PolicyValidationError::InvalidName},
        PolicyManagerNameCase{
            "CombiningSequenceAtLimit", std::string(148, 'a') + "e\xCC\x81",
            PolicyValidationError::None},
        PolicyManagerNameCase{
            "CombiningSequenceBeyondLimit", std::string(149, 'a') + "e\xCC\x81",
            PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"NonBreakingSpaceIsContent", "\xC2\xA0", PolicyValidationError::None},
        PolicyManagerNameCase{
            "NonBreakingSpaceCountsTowardsLimit", std::string(150, 'a') + "\xC2\xA0",
            PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"TruncatedSequence", "\xC3", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"UnexpectedContinuation", "\x80", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"InvalidContinuation", "\xC3\x28", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"OverlongSequence", "\xE0\x80\xAF", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"Utf16Surrogate", "\xED\xA0\x80", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"BeyondUnicodeRange", "\xF4\x90\x80\x80", PolicyValidationError::InvalidName},
        PolicyManagerNameCase{"TrailingTruncatedSequence", "Office \xC3", PolicyValidationError::InvalidName}),
    // Usa la etiqueta del caso para que CTest muestre un nombre descriptivo en cada ejecución.
    [](const ::testing::TestParamInfo<PolicyManagerNameCase>& info) {
        return std::string(info.param.label);
    });

// Agrupa el estado de las protecciones, sus modos y el resultado esperado.
struct PolicyManagerModeCase {
    const char* label;
    bool enabled;
    FilterMode urlMode;
    FilterMode downloadMode;
    PolicyValidationError expectedError;
};

class PolicyManagerModeTest : public ::testing::TestWithParam<PolicyManagerModeCase> {};

// Usa reglas válidas para aislar la validación de los modos, incluso con las protecciones apagadas.
TEST_P(PolicyManagerModeTest, RequiresKnownModesRegardlessOfEnabledState) {
    const PolicyManagerModeCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(testCase.enabled, testCase.urlMode, {"blocked.example"}),
        DownloadControlConfig(testCase.enabled, testCase.downloadMode, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(testCase.expectedError, manager.Validate(policy, 2));
}

// El valor 99 simula un modo desconocido; no pertenece a ninguna opción de FilterMode.
INSTANTIATE_TEST_SUITE_P(
    PolicyModes,
    PolicyManagerModeTest,
    ::testing::Values(
        PolicyManagerModeCase{
            "BothDenylist", true, FilterMode::Denylist, FilterMode::Denylist,
            PolicyValidationError::None},
        PolicyManagerModeCase{
            "BothAllowlist", true, FilterMode::Allowlist, FilterMode::Allowlist,
            PolicyValidationError::None},
        PolicyManagerModeCase{
            "UrlDenylistDownloadAllowlist", true, FilterMode::Denylist, FilterMode::Allowlist,
            PolicyValidationError::None},
        PolicyManagerModeCase{
            "UrlAllowlistDownloadDenylist", true, FilterMode::Allowlist, FilterMode::Denylist,
            PolicyValidationError::None},
        PolicyManagerModeCase{
            "UnknownUrlModeEnabled", true, static_cast<FilterMode>(99), FilterMode::Denylist,
            PolicyValidationError::InvalidUrlFilteringMode},
        PolicyManagerModeCase{
            "UnknownUrlModeDisabled", false, static_cast<FilterMode>(99), FilterMode::Denylist,
            PolicyValidationError::InvalidUrlFilteringMode},
        PolicyManagerModeCase{
            "UnknownDownloadModeEnabled", true, FilterMode::Denylist, static_cast<FilterMode>(99),
            PolicyValidationError::InvalidDownloadControlMode},
        PolicyManagerModeCase{
            "UnknownDownloadModeDisabled", false, FilterMode::Denylist, static_cast<FilterMode>(99),
            PolicyValidationError::InvalidDownloadControlMode}),
    // Devuelve una etiqueta descriptiva para identificar cada combinación en los resultados.
    [](const ::testing::TestParamInfo<PolicyManagerModeCase>& info) {
        return std::string(info.param.label);
    });

// Cambia únicamente la configuración de URL para comprobar el contrato de sus reglas.
struct PolicyManagerUrlRulesCase {
    const char* label;
    bool enabled;
    FilterMode mode;
    std::vector<std::string> rules;
    PolicyValidationError expectedError;
};

class PolicyManagerUrlRulesTest : public ::testing::TestWithParam<PolicyManagerUrlRulesCase> {};

// Valida dominios de la política; la decisión sobre una navegación corresponde a UrlFilter.
TEST_P(PolicyManagerUrlRulesTest, ValidatesDomainRulesBeforeAcceptingPolicy) {
    const PolicyManagerUrlRulesCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(testCase.enabled, testCase.mode, testCase.rules),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(testCase.expectedError, manager.Validate(policy, 2));
    EXPECT_EQ(testCase.rules, policy.UrlFiltering().Rules());
}

// Los dominios IDN llegan en ASCII desde el backend; no se convierten de nuevo aquí.
INSTANTIATE_TEST_SUITE_P(
    PolicyUrlRules,
    PolicyManagerUrlRulesTest,
    ::testing::Values(
        PolicyManagerUrlRulesCase{
            "EmptyDenylist", true, FilterMode::Denylist, {}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "EmptyAllowlist", true, FilterMode::Allowlist, {}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "DistinctDomains", true, FilterMode::Denylist,
            {"example.com", "sub-domain.example.com"}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "UppercaseAndTrailingDot", true, FilterMode::Denylist,
            {"EXAMPLE.COM."}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "BackendAsciiIdn", true, FilterMode::Denylist,
            {"xn--bcher-kva.de"}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "LabelAtLimit", true, FilterMode::Denylist,
            {std::string(63, 'a') + ".com"}, PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "LabelBeyondLimit", true, FilterMode::Denylist,
            {std::string(64, 'a') + ".com"}, PolicyValidationError::InvalidUrlRule},
        // Tres etiquetas de 63 y una de 61, más los tres puntos: 253 caracteres.
        PolicyManagerUrlRulesCase{
            "DomainAtLimit", true, FilterMode::Denylist,
            {std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(61, 'd')},
            PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "DomainAtLimitWithTrailingDot", true, FilterMode::Denylist,
            {std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(61, 'd') + "."},
            PolicyValidationError::None},
        PolicyManagerUrlRulesCase{
            "DomainBeyondLimit", true, FilterMode::Denylist,
            {std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(62, 'd')},
            PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "DomainBeyondLimitWithTrailingDot", true, FilterMode::Denylist,
            {std::string(63, 'a') + "." + std::string(63, 'b') + "."
                + std::string(63, 'c') + "." + std::string(62, 'd') + "."},
            PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "EmptyRule", true, FilterMode::Denylist,
            {""}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "SchemeIsNotDomain", true, FilterMode::Denylist,
            {"https://example.com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "PathIsNotDomain", true, FilterMode::Denylist,
            {"example.com/path"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "PortIsNotDomain", true, FilterMode::Denylist,
            {"example.com:443"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "WildcardIsNotDomain", true, FilterMode::Denylist,
            {"*.example.com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "EmptyLabel", true, FilterMode::Denylist,
            {"example..com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "LeadingHyphen", true, FilterMode::Denylist,
            {"-example.com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "TrailingHyphen", true, FilterMode::Denylist,
            {"example-.com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "RawUnicodeIsNotBackendAscii", true, FilterMode::Denylist,
            {"b\xC3\xBC" "cher.de"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "ExactDuplicate", true, FilterMode::Denylist,
            {"example.com", "example.com"}, PolicyValidationError::DuplicateUrlRule},
        PolicyManagerUrlRulesCase{
            "NormalizedDuplicate", true, FilterMode::Allowlist,
            {"example.com", "EXAMPLE.COM."}, PolicyValidationError::DuplicateUrlRule},
        PolicyManagerUrlRulesCase{
            "InvalidRuleWhileDisabled", false, FilterMode::Denylist,
            {"https://example.com"}, PolicyValidationError::InvalidUrlRule},
        PolicyManagerUrlRulesCase{
            "DuplicateWhileDisabled", false, FilterMode::Allowlist,
            {"example.com", "EXAMPLE.COM."}, PolicyValidationError::DuplicateUrlRule}),
    // Usa la etiqueta de cada caso como nombre descriptivo de su ejecución.
    [](const ::testing::TestParamInfo<PolicyManagerUrlRulesCase>& info) {
        return std::string(info.param.label);
    });

// Aísla las reglas de extensiones manteniendo válidos los demás campos de la política.
struct PolicyManagerDownloadRulesCase {
    const char* label;
    bool enabled;
    FilterMode mode;
    std::vector<std::string> rules;
    PolicyValidationError expectedError;
};

class PolicyManagerDownloadRulesTest
    : public ::testing::TestWithParam<PolicyManagerDownloadRulesCase> {};

// Comprueba las reglas de la política; aquí todavía no se evalúa ningún archivo descargado.
TEST_P(PolicyManagerDownloadRulesTest, ValidatesExtensionRulesBeforeAcceptingPolicy) {
    const PolicyManagerDownloadRulesCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(testCase.enabled, testCase.mode, testCase.rules),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(testCase.expectedError, manager.Validate(policy, 2));
    EXPECT_EQ(testCase.rules, policy.DownloadControl().Rules());
}

// Cubre formato, longitud y duplicados sin que la validación modifique las reglas originales.
INSTANTIATE_TEST_SUITE_P(
    PolicyDownloadRules,
    PolicyManagerDownloadRulesTest,
    ::testing::Values(
        PolicyManagerDownloadRulesCase{
            "EmptyDenylist", true, FilterMode::Denylist, {}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "EmptyAllowlist", true, FilterMode::Allowlist, {}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "DistinctExtensions", true, FilterMode::Denylist,
            {"exe", "pdf"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "UppercaseAndOptionalLeadingDot", true, FilterMode::Allowlist,
            {".EXE", "PDF", ".7Z", "C++"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "AsciiAtLimit", true, FilterMode::Denylist,
            {std::string(20, 'a')}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "AsciiBeyondLimit", true, FilterMode::Denylist,
            {std::string(21, 'a')}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "AtLimitWithLeadingDot", true, FilterMode::Denylist,
            {"." + std::string(20, 'a')}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "EmptyRule", true, FilterMode::Denylist,
            {""}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "DotOnly", true, FilterMode::Denylist,
            {"."}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "DoubleLeadingDot", true, FilterMode::Denylist,
            {"..exe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "InternalDot", true, FilterMode::Denylist,
            {"tar.gz"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "TrailingDot", true, FilterMode::Denylist,
            {"exe."}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "LeadingSpace", true, FilterMode::Denylist,
            {" exe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "TrailingSpace", true, FilterMode::Denylist,
            {"exe "}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "InternalSpace", true, FilterMode::Denylist,
            {"e xe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "TabAndNewline", true, FilterMode::Denylist,
            {"e\t\nxe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "AsciiControl", true, FilterMode::Denylist,
            {"e\x01" "xe"}, PolicyValidationError::InvalidDownloadRule},
        // La longitud explícita conserva el NUL interior y los caracteres posteriores.
        PolicyManagerDownloadRulesCase{
            "EmbeddedNull", true, FilterMode::Denylist,
            {std::string("e\0xe", 4)}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "ForwardSlash", true, FilterMode::Denylist,
            {"folder/exe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "Backslash", true, FilterMode::Denylist,
            {"folder\\exe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "Colon", true, FilterMode::Denylist,
            {"C:exe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "ExactDuplicate", true, FilterMode::Denylist,
            {"exe", "exe"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "NormalizedDuplicate", true, FilterMode::Allowlist,
            {"exe", ".EXE"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "InvalidRuleWhileDisabled", false, FilterMode::Denylist,
            {"tar.gz"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "DuplicateWhileDisabled", false, FilterMode::Allowlist,
            {"exe", ".EXE"}, PolicyValidationError::DuplicateDownloadRule}),
    // Identifica cada caso por su etiqueta en los resultados de GoogleTest y CTest.
    [](const ::testing::TestParamInfo<PolicyManagerDownloadRulesCase>& info) {
        return std::string(info.param.label);
    });

// Reutiliza el mismo TEST_P para comprobar reglas Unicode con bytes UTF-8 explícitos.
// Las conversiones especiales se contrastan con SpecialCasing.txt de Unicode 16.0.
INSTANTIATE_TEST_SUITE_P(
    PolicyDownloadUnicodeRules,
    PolicyManagerDownloadRulesTest,
    ::testing::Values(
        PolicyManagerDownloadRulesCase{
            "TwoByteCodePointAtLimit", true, FilterMode::Denylist,
            {std::string(19, 'a') + "\xC3\xB1"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "FourByteCodePointAtLimit", true, FilterMode::Denylist,
            {std::string(19, 'a') + "\xF0\x9F\x98\x80"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "FourByteCodePointBeyondLimit", true, FilterMode::Denylist,
            {std::string(20, 'a') + "\xF0\x9F\x98\x80"},
            PolicyValidationError::InvalidDownloadRule},
        // U+0130 pasa a U+0069 U+0307: un punto de código se convierte en dos.
        PolicyManagerDownloadRulesCase{
            "LowercaseExpansionAtLimit", true, FilterMode::Denylist,
            {std::string(18, 'a') + "\xC4\xB0"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "LowercaseExpansionBeyondLimit", true, FilterMode::Denylist,
            {std::string(19, 'a') + "\xC4\xB0"},
            PolicyValidationError::InvalidDownloadRule},
        // Ñ y ñ coinciden después de convertir a minúsculas y retirar el punto inicial.
        PolicyManagerDownloadRulesCase{
            "UnicodeCaseDuplicate", true, FilterMode::Allowlist,
            {".\xC3\x91", "\xC3\xB1"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "RootLocaleAsciiI", true, FilterMode::Allowlist,
            {"I", "i"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "RootLocaleDottedI", true, FilterMode::Allowlist,
            {"\xC4\xB0", "i\xCC\x87"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "DottedIAndPlainIAreDistinct", true, FilterMode::Allowlist,
            {"\xC4\xB0", "i"}, PolicyValidationError::None},
        // ΟΣ se convierte en ος: la sigma mayúscula adopta su forma final según el contexto.
        PolicyManagerDownloadRulesCase{
            "GreekFinalSigmaDuplicate", true, FilterMode::Allowlist,
            {"\xCE\x9F\xCE\xA3", "\xCE\xBF\xCF\x82"},
            PolicyValidationError::DuplicateDownloadRule},
        // Las dos formas ya minúsculas de sigma se conservan distintas: no se aplica case folding.
        PolicyManagerDownloadRulesCase{
            "GreekLowercaseSigmasAreDistinct", true, FilterMode::Allowlist,
            {"\xCE\xBF\xCF\x83", "\xCE\xBF\xCF\x82"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "AccentsArePreserved", true, FilterMode::Allowlist,
            {"e", "\xC3\xA9"}, PolicyValidationError::None},
        // é y e seguida de acento combinado no se unifican mediante normalización Unicode.
        PolicyManagerDownloadRulesCase{
            "CombiningSequenceIsPreserved", true, FilterMode::Allowlist,
            {"\xC3\xA9", "e\xCC\x81"}, PolicyValidationError::None},
        PolicyManagerDownloadRulesCase{
            "NonBreakingSpace", true, FilterMode::Denylist,
            {"e\xC2\xA0" "xe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "EmSpace", true, FilterMode::Denylist,
            {"e\xE2\x80\x83" "xe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "NarrowNonBreakingSpace", true, FilterMode::Denylist,
            {"e\xE2\x80\xAF" "xe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "C1Control", true, FilterMode::Denylist,
            {"e\xC2\x85" "xe"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "TruncatedUtf8", true, FilterMode::Denylist,
            {"ex\xC3"}, PolicyValidationError::InvalidDownloadRule},
        PolicyManagerDownloadRulesCase{
            "UnicodeDuplicateWhileDisabled", false, FilterMode::Allowlist,
            {".\xC3\x91", "\xC3\xB1"}, PolicyValidationError::DuplicateDownloadRule},
        PolicyManagerDownloadRulesCase{
            "SharpSIsNotFoldedToSs", true, FilterMode::Allowlist,
            {"\xC3\x9F", "ss"}, PolicyValidationError::None}),
    // El prefijo de esta instancia distingue los casos Unicode de los casos básicos.
    [](const ::testing::TestParamInfo<PolicyManagerDownloadRulesCase>& info) {
        return std::string(info.param.label);
    });

// Si fallan la identidad y el nombre, prevalece la comprobación de identidad.
TEST(PolicyManagerTest, InvalidPolicyIdTakesPriorityOverInvalidName) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        0, 2, "",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(PolicyValidationError::InvalidPolicyId, manager.Validate(policy, 2));
}

// Los modos se comprueban antes de examinar el contenido de las reglas de dominio.
TEST(PolicyManagerTest, InvalidModeTakesPriorityOverInvalidUrlRule) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(true, static_cast<FilterMode>(99), {"https://example.com"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(PolicyValidationError::InvalidUrlFilteringMode, manager.Validate(policy, 2));
}

// Con reglas inválidas en ambas protecciones, se devuelve primero el error de dominio.
TEST(PolicyManagerTest, InvalidUrlRuleTakesPriorityOverInvalidDownloadRule) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 2, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"https://example.com"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"tar.gz"}),
        timestamp, timestamp);
    const PolicyManager manager;

    EXPECT_EQ(PolicyValidationError::InvalidUrlRule, manager.Validate(policy, 2));
}

// El primer contexto recibe generación positiva y conserva IDs de instalación de 64 bits.
TEST(PolicyManagerTest, BeginsContextWithNewGenerationAndInstallationIdentity) {
    constexpr std::int64_t deviceId = 5'000'000'001;
    constexpr std::int64_t companyId = 6'000'000'002;
    PolicyManager manager;

    const auto context = manager.BeginContext(deviceId, companyId);

    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(std::uint64_t{1}, context->Generation());
    EXPECT_EQ(deviceId, context->DeviceId());
    EXPECT_EQ(companyId, context->CompanyId());
}

// Repetir dispositivo y compañía crea otra identidad local, como al volver a iniciar sesión.
TEST(PolicyManagerTest, BeginsDistinctContextForSameInstallation) {
    PolicyManager manager;
    const auto first = manager.BeginContext(10, 20);
    const auto second = manager.BeginContext(10, 20);

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->Generation() + 1, second->Generation());
    EXPECT_EQ(first->DeviceId(), second->DeviceId());
    EXPECT_EQ(first->CompanyId(), second->CompanyId());
    EXPECT_FALSE(*first == *second);
}

struct PolicyManagerInvalidContextCase {
    const char* label;
    std::int64_t deviceId;
    std::int64_t companyId;
};

class PolicyManagerInvalidContextTest
    : public ::testing::TestWithParam<PolicyManagerInvalidContextCase> {};

// Un identificador no positivo impide obtener un contexto.
TEST_P(PolicyManagerInvalidContextTest, RejectsNonPositiveInstallationIdentifiers) {
    const PolicyManagerInvalidContextCase& testCase = GetParam();
    PolicyManager manager;

    const auto context = manager.BeginContext(testCase.deviceId, testCase.companyId);

    EXPECT_FALSE(context.has_value());
}

INSTANTIATE_TEST_SUITE_P(
    InvalidContextIdentifiers,
    PolicyManagerInvalidContextTest,
    ::testing::Values(
        PolicyManagerInvalidContextCase{"ZeroDeviceId", 0, 20},
        PolicyManagerInvalidContextCase{"NegativeDeviceId", -1, 20},
        PolicyManagerInvalidContextCase{"ZeroCompanyId", 10, 0},
        PolicyManagerInvalidContextCase{"NegativeCompanyId", 10, -1}),
    // Identifica el campo y el límite inválidos de cada ejecución.
    [](const ::testing::TestParamInfo<PolicyManagerInvalidContextCase>& info) {
        return std::string(info.param.label);
    });

// Los intentos rechazados no avanzan el contador entre dos contextos válidos.
TEST(PolicyManagerTest, RejectedContextDoesNotConsumeGeneration) {
    PolicyManager manager;
    const auto first = manager.BeginContext(10, 20);
    ASSERT_TRUE(first.has_value());

    EXPECT_FALSE(manager.BeginContext(0, 20).has_value());
    EXPECT_FALSE(manager.BeginContext(10, -1).has_value());

    const auto second = manager.BeginContext(10, 20);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->Generation() + 1, second->Generation());
}

// Antes de iniciar una sesión e instalación, ningún contexto es el actual.
TEST(PolicyManagerTest, HasNoCurrentContextBeforeBeginningOne) {
    const PolicyManager manager;

    EXPECT_FALSE(manager.IsCurrentContext(SecurityContext(1, 10, 20)));
}

struct PolicyManagerContextMatchCase {
    const char* label;
    std::uint64_t generation;
    std::int64_t deviceId;
    std::int64_t companyId;
    bool expectedCurrent;
};

class PolicyManagerContextMatchTest
    : public ::testing::TestWithParam<PolicyManagerContextMatchCase> {};

// Comprueba la identidad contra el estado realmente publicado por BeginContext.
TEST_P(PolicyManagerContextMatchTest, MatchesOnlyTheCompleteCurrentContext) {
    const PolicyManagerContextMatchCase& testCase = GetParam();
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    const SecurityContext received(testCase.generation, testCase.deviceId, testCase.companyId);

    EXPECT_EQ(testCase.expectedCurrent, manager.IsCurrentContext(received));
}

INSTANTIATE_TEST_SUITE_P(
    CurrentContextIdentity,
    PolicyManagerContextMatchTest,
    ::testing::Values(
        PolicyManagerContextMatchCase{"SameContext", 1, 10, 20, true},
        PolicyManagerContextMatchCase{"DifferentGeneration", 2, 10, 20, false},
        PolicyManagerContextMatchCase{"DifferentDevice", 1, 11, 20, false},
        PolicyManagerContextMatchCase{"DifferentCompany", 1, 10, 21, false}),
    // Identifica qué parte del contexto recibido cambia respecto al publicado.
    [](const ::testing::TestParamInfo<PolicyManagerContextMatchCase>& info) {
        return std::string(info.param.label);
    });

// Una referencia a la sesión anterior deja de coincidir aunque conserve dispositivo y compañía.
TEST(PolicyManagerTest, PreviousContextStopsMatchingAfterBeginningAnother) {
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    ASSERT_TRUE(manager.IsCurrentContext(*previous));

    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_FALSE(manager.IsCurrentContext(*previous));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// Los guards de una creación rechazada conservan el contexto que ya estaba publicado.
TEST(PolicyManagerTest, RejectedContextDoesNotReplaceCurrentContext) {
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_FALSE(manager.BeginContext(0, 20).has_value());
    EXPECT_FALSE(manager.BeginContext(10, -1).has_value());

    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// Sin contexto publicado no hay nada que cerrar.
TEST(PolicyManagerTest, CannotEndContextBeforeBeginningOne) {
    PolicyManager manager;

    EXPECT_FALSE(manager.EndContext(SecurityContext(1, 10, 20)));
}

// Tras cerrar el contexto actual, una consulta posterior ya no lo reconoce como vigente.
TEST(PolicyManagerTest, EndingCurrentContextRemovesItFromCurrentState) {
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    ASSERT_TRUE(manager.IsCurrentContext(*current));

    EXPECT_TRUE(manager.EndContext(*current));
    EXPECT_FALSE(manager.IsCurrentContext(*current));
}

// Repetir el cierre no vuelve a retirar un estado ni comunica un segundo cierre efectivo.
TEST(PolicyManagerTest, EndingAlreadyClosedContextReturnsFalse) {
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    ASSERT_TRUE(manager.EndContext(*current));

    EXPECT_FALSE(manager.EndContext(*current));
    EXPECT_FALSE(manager.IsCurrentContext(*current));
}

// Un resultado tardío de la sesión anterior no termina la nueva de la misma instalación.
TEST(PolicyManagerTest, EndingPreviousContextDoesNotCloseNewContext) {
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_FALSE(manager.EndContext(*previous));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// Cerrar y volver a iniciar no reutiliza la identidad que conservan las peticiones anteriores.
TEST(PolicyManagerTest, BeginningAfterEndDoesNotReuseGeneration) {
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    ASSERT_TRUE(manager.EndContext(*previous));

    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(previous->Generation() + 1, current->Generation());
    EXPECT_FALSE(manager.IsCurrentContext(*previous));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

struct PolicyManagerForeignEndContextCase {
    const char* label;
    std::int64_t deviceId;
    std::int64_t companyId;
};

class PolicyManagerForeignEndContextTest
    : public ::testing::TestWithParam<PolicyManagerForeignEndContextCase> {};

// Incluso con la generación correcta, dispositivo y compañía deben corresponder al contexto actual.
TEST_P(PolicyManagerForeignEndContextTest, DoesNotCloseContextForForeignInstallationIdentity) {
    const PolicyManagerForeignEndContextCase& testCase = GetParam();
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    const SecurityContext foreign(current->Generation(), testCase.deviceId, testCase.companyId);

    EXPECT_FALSE(manager.EndContext(foreign));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

INSTANTIATE_TEST_SUITE_P(
    ForeignEndContextIdentity,
    PolicyManagerForeignEndContextTest,
    ::testing::Values(
        PolicyManagerForeignEndContextCase{"DifferentDevice", 11, 20},
        PolicyManagerForeignEndContextCase{"DifferentCompany", 10, 21}),
    // Nombra cada intento de cierre por la parte ajena de la identidad de instalación.
    [](const ::testing::TestParamInfo<PolicyManagerForeignEndContextCase>& info) {
        return std::string(info.param.label);
    });

// Una función vacía no permite consultar el tiempo y debe rechazarse al construir el gestor.
TEST(PolicyManagerTest, RejectsEmptyClockFunction) {
    EXPECT_THROW(PolicyManager{PolicyManager::NowFunction{}}, std::invalid_argument);
}

// Una política completa de la compañía actual puede publicarse sin cambiar el contexto.
TEST(PolicyManagerTest, PublishesValidPolicyForCurrentContext) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*current, policy));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// Tener una política válida no permite publicarla antes de establecer un contexto actual.
TEST(PolicyManagerTest, RejectsPublicationWithoutCurrentContext) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const SecurityContext received(1, 10, 20);

    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(received, policy));
    EXPECT_FALSE(manager.IsCurrentContext(received));
}

// La respuesta de una consulta iniciada antes del cierre no recupera el contexto terminado.
TEST(PolicyManagerTest, RejectsPublicationForEndedContext) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto ended = manager.BeginContext(10, 20);
    ASSERT_TRUE(ended.has_value());
    ASSERT_TRUE(manager.EndContext(*ended));

    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(*ended, policy));
    EXPECT_FALSE(manager.IsCurrentContext(*ended));
}

// Una respuesta anterior no sustituye el contexto nuevo aunque coincidan dispositivo y compañía.
TEST(PolicyManagerTest, RejectsPublicationForPreviousContext) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(*previous, policy));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// La correspondencia del contexto no permite publicar una política con datos inválidos.
TEST(PolicyManagerTest, RejectsPublicationOfInvalidPolicy) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        0, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*current, policy));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// La compañía esperada procede del contexto, no de la política recibida.
TEST(PolicyManagerTest, RejectsPublicationOfPolicyFromForeignCompany) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 21, "Foreign company policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    EXPECT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*current, policy));
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

// Simula un cierre entre la validación inicial y la sustitución final del estado.
TEST(PolicyManagerTest, RejectsPublicationIfContextEndsDuringPreparation) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    std::function<void()> onClockRead;

    // La consulta del reloj ejecuta la acción de prueba en el mismo hilo, fuera del mutex del gestor.
    PolicyManager manager([&onClockRead] {
        if (onClockRead) {
            onClockRead();
        }
        return PolicyManager::Clock::time_point{};
    });
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    bool ended = false;

    // Las referencias siguen vivas durante PublishPolicy; el contexto se cierra al consultar el reloj.
    onClockRead = [&manager, &current, &ended] {
        ended = manager.EndContext(*current);
    };

    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(*current, policy));
    EXPECT_TRUE(ended);
    EXPECT_FALSE(manager.IsCurrentContext(*current));
}

// La comprobación final debe conservar la sesión nueva y rechazar la respuesta de la anterior.
TEST(PolicyManagerTest, RejectsPublicationIfContextChangesDuringPreparation) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    std::function<void()> onClockRead;

    // El reloj permite fijar el cambio de contexto antes del intercambio, sin esperas ni hilos adicionales.
    PolicyManager manager([&onClockRead] {
        if (onClockRead) {
            onClockRead();
        }
        return PolicyManager::Clock::time_point{};
    });
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    std::optional<SecurityContext> current;

    // Las referencias locales siguen vivas durante la consulta del reloj y la comprobación posterior.
    onClockRead = [&manager, &current] {
        current = manager.BeginContext(10, 20);
    };

    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(*previous, policy));
    ASSERT_TRUE(current.has_value());
    EXPECT_TRUE(manager.IsCurrentContext(*current));
    EXPECT_FALSE(manager.IsCurrentContext(*previous));
}

// Sin contexto no existe un estado que pueda conservar un lector.
TEST(PolicyManagerTest, ReadsEmptyStateBeforeBeginningContext) {
    const PolicyManager manager;

    EXPECT_FALSE(manager.ReadState());
}

// Iniciar el contexto no inventa una política ni un instante de validación.
TEST(PolicyManagerTest, ReadsContextWithoutPolicyBeforeFirstPublication) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const auto state = manager.ReadState();

    ASSERT_TRUE(state);
    EXPECT_EQ(*context, state->context);
    EXPECT_FALSE(state->policy);
    EXPECT_FALSE(state->validatedAt.has_value());
}

// La lectura reúne los datos de la política, su contexto y el instante del reloj inyectado.
TEST(PolicyManagerTest, ReadsCompletePublishedPolicyAndValidationInstant) {
    const PolicySnapshot::Timestamp createdAt{std::chrono::microseconds{1'700'000'000'123'456}};
    const auto updatedAt = createdAt + std::chrono::seconds{1};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(false, FilterMode::Allowlist, {"allowed.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        createdAt, updatedAt);
    const PolicyManager::Clock::time_point validatedAt{std::chrono::seconds{42}};

    // El reloj conserva por valor un instante fijo, independiente de las fechas del backend.
    PolicyManager manager([validatedAt] { return validatedAt; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));

    const auto state = manager.ReadState();

    ASSERT_TRUE(state);
    ASSERT_TRUE(state->policy);
    ASSERT_TRUE(state->validatedAt.has_value());
    EXPECT_EQ(*context, state->context);
    EXPECT_EQ(validatedAt, *state->validatedAt);
    EXPECT_EQ(policy.Id(), state->policy->Id());
    EXPECT_EQ(policy.CompanyId(), state->policy->CompanyId());
    EXPECT_EQ(policy.Name(), state->policy->Name());
    EXPECT_EQ(createdAt, state->policy->CreatedAt());
    EXPECT_EQ(updatedAt, state->policy->UpdatedAt());
    EXPECT_FALSE(state->policy->UrlFiltering().Enabled());
    EXPECT_EQ(FilterMode::Allowlist, state->policy->UrlFiltering().Mode());
    EXPECT_EQ(policy.UrlFiltering().Rules(), state->policy->UrlFiltering().Rules());
    EXPECT_TRUE(state->policy->DownloadControl().Enabled());
    EXPECT_EQ(FilterMode::Denylist, state->policy->DownloadControl().Mode());
    EXPECT_EQ(policy.DownloadControl().Rules(), state->policy->DownloadControl().Rules());
}

// Publicar otra política no modifica el estado ni el instante que ya conserva un lector.
TEST(PolicyManagerTest, RetainedStateKeepsPreviousPolicyAndValidationInstantAfterReplacement) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot firstPolicy(
        1, 20, "First policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        timestamp, timestamp);
    const PolicySnapshot secondPolicy(
        2, 20, "Second policy",
        UrlFilteringConfig(true, FilterMode::Allowlist, {"allowed.example"}),
        DownloadControlConfig(false, FilterMode::Allowlist, {"pdf"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{std::chrono::seconds{10}};
    const auto firstValidatedAt = now;

    // La variable de tiempo sigue viva durante las dos publicaciones secuenciales.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, firstPolicy));
    const auto firstState = manager.ReadState();

    now += std::chrono::minutes{1};
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, secondPolicy));
    const auto secondState = manager.ReadState();

    ASSERT_TRUE(firstState);
    ASSERT_TRUE(secondState);
    ASSERT_TRUE(firstState->policy);
    ASSERT_TRUE(secondState->policy);
    ASSERT_TRUE(firstState->validatedAt.has_value());
    ASSERT_TRUE(secondState->validatedAt.has_value());
    EXPECT_NE(firstState.get(), secondState.get());
    EXPECT_EQ(*context, firstState->context);
    EXPECT_EQ(*context, secondState->context);
    EXPECT_EQ(firstPolicy.Id(), firstState->policy->Id());
    EXPECT_EQ(firstPolicy.Name(), firstState->policy->Name());
    EXPECT_EQ(secondPolicy.Id(), secondState->policy->Id());
    EXPECT_EQ(secondPolicy.Name(), secondState->policy->Name());
    EXPECT_EQ(firstValidatedAt, *firstState->validatedAt);
    EXPECT_EQ(now, *secondState->validatedAt);
}

// Conservar los datos anteriores no mantiene activo un contexto que ya se ha cerrado.
TEST(PolicyManagerTest, RetainedStateRemainsReadableAfterContextEnds) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    const auto retained = manager.ReadState();

    ASSERT_TRUE(manager.EndContext(*context));

    EXPECT_FALSE(manager.ReadState());
    EXPECT_FALSE(manager.IsCurrentContext(*context));
    ASSERT_TRUE(retained);
    ASSERT_TRUE(retained->policy);
    EXPECT_EQ(*context, retained->context);
    EXPECT_EQ(policy.Id(), retained->policy->Id());
    EXPECT_EQ(policy.Name(), retained->policy->Name());
}

// Sin contexto actual no se devuelve un estado de política utilizable.
TEST(PolicyManagerTest, ReadsInvalidPolicyContextBeforeBeginningOne) {
    const PolicyManager manager;

    const auto result = manager.ReadPolicy(SecurityContext(1, 10, 20));

    EXPECT_EQ(PolicyAvailability::InvalidContext, result.availability);
    EXPECT_FALSE(result.state);
}

// Un contexto iniciado conserva su estado aunque todavía no disponga de política.
TEST(PolicyManagerTest, ReadsUnavailablePolicyBeforeFirstPublication) {
    PolicyManager manager;
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    const auto result = manager.ReadPolicy(*context);

    EXPECT_EQ(PolicyAvailability::Unavailable, result.availability);
    ASSERT_TRUE(result.state);
    EXPECT_EQ(*context, result.state->context);
    EXPECT_FALSE(result.state->policy);
    EXPECT_FALSE(result.state->validatedAt.has_value());
}

// La consulta de una sesión anterior no recibe el estado de la sesión nueva.
TEST(PolicyManagerTest, ReadsInvalidPolicyContextForPreviousSession) {
    PolicyManager manager;
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());

    const auto result = manager.ReadPolicy(*previous);

    EXPECT_EQ(PolicyAvailability::InvalidContext, result.availability);
    EXPECT_FALSE(result.state);
    EXPECT_TRUE(manager.IsCurrentContext(*current));
}

struct PolicyManagerLifetimeCase {
    const char* label;
    PolicyManager::Clock::duration elapsed;
    PolicyAvailability expectedAvailability;
};

class PolicyManagerLifetimeTest : public ::testing::TestWithParam<PolicyManagerLifetimeCase> {};

// El reloj se adelanta sin esperas; la disponibilidad y el estado devuelto deben corresponderse.
TEST_P(PolicyManagerLifetimeTest, ExpiresExactlyAtFifteenMinutes) {
    const PolicyManagerLifetimeCase& testCase = GetParam();
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};
    const auto publishedAt = now;

    // La variable de tiempo sigue viva durante la publicación y la consulta secuenciales.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    const auto publishedState = manager.ReadState();

    now += testCase.elapsed;
    const auto result = manager.ReadPolicy(*context);

    EXPECT_EQ(testCase.expectedAvailability, result.availability);
    EXPECT_EQ(publishedState, result.state);
    ASSERT_TRUE(result.state);
    ASSERT_TRUE(result.state->policy);
    ASSERT_TRUE(result.state->validatedAt.has_value());
    EXPECT_EQ(*context, result.state->context);
    EXPECT_EQ(policy.Id(), result.state->policy->Id());
    EXPECT_EQ(publishedAt, *result.state->validatedAt);
}

// Una unidad de duración representa el menor incremento del reloj utilizado por el núcleo.
INSTANTIATE_TEST_SUITE_P(
    PolicyLifetime,
    PolicyManagerLifetimeTest,
    ::testing::Values(
        PolicyManagerLifetimeCase{
            "JustPublished", PolicyManager::Clock::duration{0}, PolicyAvailability::Available},
        PolicyManagerLifetimeCase{
            "ImmediatelyBeforeDeadline", std::chrono::minutes{15} - PolicyManager::Clock::duration{1},
            PolicyAvailability::Available},
        PolicyManagerLifetimeCase{
            "ExactlyAtDeadline", std::chrono::minutes{15}, PolicyAvailability::Expired},
        PolicyManagerLifetimeCase{
            "ImmediatelyAfterDeadline", std::chrono::minutes{15} + PolicyManager::Clock::duration{1},
            PolicyAvailability::Expired}),
    // Nombra cada ejecución según su posición respecto al límite de caducidad.
    [](const ::testing::TestParamInfo<PolicyManagerLifetimeCase>& info) {
        return std::string(info.param.label);
    });

// Una obtención válida renueva el plazo aunque no cambie ningún dato del backend.
TEST(PolicyManagerTest, SamePolicyPublicationRenewsValidity) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};

    // El tiempo se controla desde la prueba y permanece vivo durante todas las consultas.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    const auto original = manager.ReadState();
    ASSERT_TRUE(original);
    ASSERT_TRUE(original->validatedAt.has_value());
    const auto originalValidatedAt = *original->validatedAt;

    now += std::chrono::minutes{14};
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    const auto renewed = manager.ReadState();
    ASSERT_TRUE(renewed);
    ASSERT_TRUE(renewed->policy);
    ASSERT_TRUE(renewed->validatedAt.has_value());
    EXPECT_EQ(now, *renewed->validatedAt);
    EXPECT_EQ(timestamp, renewed->policy->UpdatedAt());
    EXPECT_EQ(originalValidatedAt, *original->validatedAt);

    // En el minuto 15 solo ha transcurrido un minuto desde la renovación del minuto 14.
    now += std::chrono::minutes{1};
    const auto available = manager.ReadPolicy(*context);
    EXPECT_EQ(PolicyAvailability::Available, available.availability);
    EXPECT_EQ(renewed, available.state);

    // En el minuto 29 se alcanzan los nuevos 15 minutos de vigencia.
    now += std::chrono::minutes{14};
    EXPECT_EQ(PolicyAvailability::Expired, manager.ReadPolicy(*context).availability);
}

// Consultar y conservar una referencia no inicia otro plazo de vigencia.
TEST(PolicyManagerTest, ReadingPolicyDoesNotRenewValidity) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};
    const auto publishedAt = now;

    // El reloj permite separar la lectura del minuto 14 de la caducidad del minuto 15.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));

    now += std::chrono::minutes{14};
    const auto retained = manager.ReadPolicy(*context);
    ASSERT_EQ(PolicyAvailability::Available, retained.availability);
    ASSERT_TRUE(retained.state);

    now += std::chrono::minutes{1};
    const auto expired = manager.ReadPolicy(*context);

    EXPECT_EQ(PolicyAvailability::Expired, expired.availability);
    EXPECT_EQ(retained.state, expired.state);
    ASSERT_TRUE(retained.state->validatedAt.has_value());
    EXPECT_EQ(publishedAt, *retained.state->validatedAt);
}

// Una regla inválida rechaza toda la actualización y conserva el plazo de la política anterior.
TEST(PolicyManagerTest, InvalidPolicyPublicationDoesNotRenewValidity) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        timestamp, timestamp);
    const PolicySnapshot invalidPolicy(
        2, 20, "Rejected update",
        UrlFilteringConfig(false, FilterMode::Allowlist, {"allowed.example", "https://invalid.example"}),
        DownloadControlConfig(false, FilterMode::Allowlist, {"pdf"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};

    // Las dos respuestas se evalúan con un reloj controlado, sin esperas reales.
    PolicyManager manager([&now] { return now; });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    const auto original = manager.ReadState();
    ASSERT_TRUE(original);

    now += std::chrono::minutes{14};
    EXPECT_EQ(PolicyPublicationResult::InvalidPolicy, manager.PublishPolicy(*context, invalidPolicy));
    EXPECT_EQ(original, manager.ReadState());
    EXPECT_EQ(PolicyAvailability::Available, manager.ReadPolicy(*context).availability);

    now += std::chrono::minutes{1};
    const auto result = manager.ReadPolicy(*context);
    EXPECT_EQ(PolicyAvailability::Expired, result.availability);
    EXPECT_EQ(original, result.state);
    ASSERT_TRUE(result.state);
    ASSERT_TRUE(result.state->policy);
    EXPECT_EQ(policy.Id(), result.state->policy->Id());
}

// Una respuesta tardía de otra sesión no renueva la política de la sesión actual.
TEST(PolicyManagerTest, PreviousContextPublicationDoesNotRenewCurrentPolicy) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe", "zip"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};

    // La sesión nueva empieza en el minuto 5 y su política debe caducar en el minuto 20.
    PolicyManager manager([&now] { return now; });
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    now += std::chrono::minutes{5};
    const auto current = manager.BeginContext(10, 20);
    ASSERT_TRUE(current.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*current, policy));
    const auto original = manager.ReadState();
    ASSERT_TRUE(original);

    now += std::chrono::minutes{9};
    EXPECT_EQ(PolicyPublicationResult::InvalidContext, manager.PublishPolicy(*previous, policy));
    EXPECT_EQ(original, manager.ReadState());
    EXPECT_EQ(PolicyAvailability::Available, manager.ReadPolicy(*current).availability);

    now += std::chrono::minutes{6};
    EXPECT_EQ(PolicyAvailability::Expired, manager.ReadPolicy(*current).availability);
}

// Un cierre mientras se consulta el reloj impide devolver la política como disponible.
TEST(PolicyManagerTest, ReadPolicyRejectsContextEndedDuringClockRead) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        timestamp, timestamp);
    std::function<void()> onClockRead;

    // La acción se configura después de publicar y se ejecuta en el mismo hilo de la consulta.
    PolicyManager manager([&onClockRead] {
        if (onClockRead) {
            onClockRead();
        }
        return PolicyManager::Clock::time_point{};
    });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, policy));
    bool ended = false;

    // El gestor, el contexto y la variable de resultado siguen vivos durante ReadPolicy.
    onClockRead = [&manager, &context, &ended] {
        ended = manager.EndContext(*context);
    };

    const auto result = manager.ReadPolicy(*context);

    EXPECT_TRUE(ended);
    EXPECT_EQ(PolicyAvailability::InvalidContext, result.availability);
    EXPECT_FALSE(result.state);
    EXPECT_FALSE(manager.ReadState());
}

// La lectura de la sesión anterior no entrega el estado de una sesión iniciada durante la consulta.
TEST(PolicyManagerTest, ReadPolicyRejectsContextChangedDuringClockRead) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot policy(
        1, 20, "Office policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        timestamp, timestamp);
    std::function<void()> onClockRead;

    // La consulta del reloj provoca el cambio una vez alcanzada la lectura de la política anterior.
    PolicyManager manager([&onClockRead] {
        if (onClockRead) {
            onClockRead();
        }
        return PolicyManager::Clock::time_point{};
    });
    const auto previous = manager.BeginContext(10, 20);
    ASSERT_TRUE(previous.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*previous, policy));
    std::optional<SecurityContext> current;

    // Las referencias locales siguen vivas durante la creación del contexto y su comprobación.
    onClockRead = [&manager, &current] {
        current = manager.BeginContext(10, 20);
    };

    const auto result = manager.ReadPolicy(*previous);

    EXPECT_EQ(PolicyAvailability::InvalidContext, result.availability);
    EXPECT_FALSE(result.state);
    ASSERT_TRUE(current.has_value());
    EXPECT_TRUE(manager.IsCurrentContext(*current));
    const auto currentState = manager.ReadState();
    ASSERT_TRUE(currentState);
    EXPECT_EQ(*current, currentState->context);
    EXPECT_FALSE(currentState->policy);
}

// La disponibilidad calculada debe acompañar al estado leído, aunque se publique otro entretanto.
TEST(PolicyManagerTest, ReadPolicyKeepsAvailabilityAndStateCoherentDuringReplacement) {
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    const PolicySnapshot firstPolicy(
        1, 20, "First policy",
        UrlFilteringConfig(true, FilterMode::Denylist, {"blocked.example"}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
        timestamp, timestamp);
    const PolicySnapshot secondPolicy(
        2, 20, "Second policy",
        UrlFilteringConfig(true, FilterMode::Allowlist, {"allowed.example"}),
        DownloadControlConfig(false, FilterMode::Allowlist, {"pdf"}),
        timestamp, timestamp);
    PolicyManager::Clock::time_point now{};
    std::function<void()> onClockRead;

    // Extrae y vacía la acción antes de ejecutarla: la publicación interna consultará el reloj de nuevo.
    PolicyManager manager([&now, &onClockRead] {
        auto action = std::exchange(onClockRead, {});
        if (action) {
            action();
        }
        return now;
    });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, firstPolicy));
    const auto firstState = manager.ReadState();
    ASSERT_TRUE(firstState);
    now += std::chrono::minutes{15};
    PolicyPublicationResult replacementResult = PolicyPublicationResult::InvalidContext;

    // Las referencias siguen vivas durante la publicación anidada y la devolución de la lectura original.
    onClockRead = [&manager, &context, &secondPolicy, &replacementResult] {
        replacementResult = manager.PublishPolicy(*context, secondPolicy);
    };

    const auto result = manager.ReadPolicy(*context);

    ASSERT_EQ(PolicyPublicationResult::Published, replacementResult);
    EXPECT_EQ(PolicyAvailability::Expired, result.availability);
    EXPECT_EQ(firstState, result.state);
    ASSERT_TRUE(result.state);
    ASSERT_TRUE(result.state->policy);
    EXPECT_EQ(firstPolicy.Id(), result.state->policy->Id());

    const auto currentState = manager.ReadState();
    ASSERT_TRUE(currentState);
    ASSERT_TRUE(currentState->policy);
    ASSERT_TRUE(currentState->validatedAt.has_value());
    EXPECT_EQ(secondPolicy.Id(), currentState->policy->Id());
    EXPECT_EQ(now, *currentState->validatedAt);
    EXPECT_EQ(PolicyAvailability::Available, manager.ReadPolicy(*context).availability);
}

// Un lector y un publicador trabajan en hilos distintos conservando estados anteriores.
TEST(PolicyManagerTest, ConcurrentPublicationAndReadsKeepCompleteImmutableStates) {
    constexpr std::int64_t rounds = 64;
    const PolicySnapshot::Timestamp timestamp{std::chrono::microseconds{1'700'000'000'123'456}};
    std::atomic<std::int64_t> clockTicks{1};

    // El reloj comparte solo un contador atómico; admite las consultas simultáneas de ambos hilos.
    PolicyManager manager([&clockTicks] {
        return PolicyManager::Clock::time_point{PolicyManager::Clock::duration{clockTicks.load()}};
    });
    const auto context = manager.BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());

    // Cada versión codifica su identidad en campos distintos para detectar publicaciones parciales.
    const auto makePolicy = [timestamp](std::int64_t version) {
        const auto text = std::to_string(version);
        const bool even = version % 2 == 0;
        const auto createdAt = timestamp + std::chrono::microseconds{version};
        return PolicySnapshot(
            version, 20, "Policy " + text,
            UrlFilteringConfig(even, even ? FilterMode::Allowlist : FilterMode::Denylist,
                {"domain" + text + ".example"}),
            DownloadControlConfig(!even, even ? FilterMode::Denylist : FilterMode::Allowlist,
                {"ext" + text}),
            createdAt, createdAt + std::chrono::microseconds{1});
    };
    ASSERT_EQ(PolicyPublicationResult::Published, manager.PublishPolicy(*context, makePolicy(1)));

    // El estado leído debe contener una única versión, incluidos sus metadatos y la vigencia local.
    const auto isCoherent = [context, timestamp, rounds](const std::shared_ptr<const PolicyManager::State>& state) {
        if (!state || !state->policy || !state->validatedAt.has_value()) {
            return false;
        }
        const auto& policy = *state->policy;
        const auto version = policy.Id();
        if (version < 1 || version > rounds + 1) {
            return false;
        }
        const auto text = std::to_string(version);
        const bool even = version % 2 == 0;
        const auto createdAt = timestamp + std::chrono::microseconds{version};
        const PolicyManager::Clock::time_point validatedAt{PolicyManager::Clock::duration{version}};
        return state->context == *context
            && policy.CompanyId() == 20
            && policy.Name() == "Policy " + text
            && policy.CreatedAt() == createdAt
            && policy.UpdatedAt() == createdAt + std::chrono::microseconds{1}
            && *state->validatedAt == validatedAt
            && policy.UrlFiltering().Enabled() == even
            && policy.UrlFiltering().Mode() == (even ? FilterMode::Allowlist : FilterMode::Denylist)
            && policy.UrlFiltering().Rules() == std::vector<std::string>{"domain" + text + ".example"}
            && policy.DownloadControl().Enabled() == !even
            && policy.DownloadControl().Mode() == (even ? FilterMode::Denylist : FilterMode::Allowlist)
            && policy.DownloadControl().Rules() == std::vector<std::string>{"ext" + text};
    };

    std::barrier<> rendezvous{2};
    bool publicationsSucceeded = true;
    bool readsCoherent = true;

    // Las referencias permanecen vivas hasta join; solo el publicador escribe su indicador de resultado.
    std::jthread publisher([rounds, &manager, &context, &clockTicks, &makePolicy,
                              &rendezvous, &publicationsSucceeded] {
        for (std::int64_t version = 2; version <= rounds + 1; ++version) {
            rendezvous.arrive_and_wait();
            try {
                clockTicks.store(version);
                if (manager.PublishPolicy(*context, makePolicy(version))
                    != PolicyPublicationResult::Published) {
                    publicationsSucceeded = false;
                }
            } catch (...) {
                publicationsSucceeded = false;
            }
            rendezvous.arrive_and_wait();
        }
    });

    for (std::int64_t round = 0; round < rounds; ++round) {
        const auto retained = manager.ReadState();
        const auto retainedVersion = retained && retained->policy ? retained->policy->Id() : 0;
        rendezvous.arrive_and_wait();
        try {
            for (int sample = 0; sample < 8; ++sample) {
                const auto result = manager.ReadPolicy(*context);
                if (result.availability != PolicyAvailability::Available || !isCoherent(result.state)) {
                    readsCoherent = false;
                }
            }
        } catch (...) {
            readsCoherent = false;
        }
        rendezvous.arrive_and_wait();
        try {
            if (!isCoherent(retained) || retained->policy->Id() != retainedVersion) {
                readsCoherent = false;
            }
        } catch (...) {
            readsCoherent = false;
        }
    }
    publisher.join();

    EXPECT_TRUE(publicationsSucceeded);
    EXPECT_TRUE(readsCoherent);
    const auto finalState = manager.ReadState();
    ASSERT_TRUE(finalState);
    ASSERT_TRUE(finalState->policy);
    EXPECT_EQ(rounds + 1, finalState->policy->Id());
}

// Cuatro hilos crean generaciones y solicitan cierres de identidades que ya han sustituido.
TEST(PolicyManagerTest, ConcurrentContextChangesKeepUniqueGenerationsAndRejectStaleEnds) {
    constexpr std::size_t workerCount = 4;
    constexpr std::size_t contextsPerWorker = 32;
    PolicyManager manager;
    const auto initial = manager.BeginContext(10, 20);
    ASSERT_TRUE(initial.has_value());
    std::array<std::vector<SecurityContext>, workerCount> contexts;
    std::array<bool, workerCount> workersSucceeded;
    workersSucceeded.fill(true);
    for (auto& row : contexts) {
        row.reserve(contextsPerWorker);
    }
    std::latch start{1};
    std::vector<std::jthread> workers;
    workers.reserve(workerCount);

    try {
        for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
            // Cada hilo escribe en su propia fila e indicador; todos los objetos viven hasta join.
            workers.emplace_back([workerIndex, initial, contextsPerWorker,
                                     &manager, &contexts, &workersSucceeded, &start] {
                start.wait();
                try {
                    SecurityContext previous = *initial;
                    for (std::size_t index = 0; index < contextsPerWorker; ++index) {
                        const auto current = manager.BeginContext(10, 20);
                        if (!current.has_value()) {
                            workersSucceeded[workerIndex] = false;
                            continue;
                        }
                        contexts[workerIndex].push_back(*current);
                        if (manager.EndContext(previous)) {
                            workersSucceeded[workerIndex] = false;
                        }
                        previous = *current;
                    }
                } catch (...) {
                    workersSucceeded[workerIndex] = false;
                }
            });
        }
    } catch (...) {
        // Si no se puede crear algún hilo, se liberan y unen los ya iniciados antes de propagar el error.
        start.count_down();
        for (auto& worker : workers) {
            worker.join();
        }
        throw;
    }
    start.count_down();
    for (auto& worker : workers) {
        worker.join();
    }

    std::unordered_set<std::uint64_t> generations{initial->Generation()};
    const SecurityContext* newest = &*initial;
    for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
        EXPECT_TRUE(workersSucceeded[workerIndex]);
        EXPECT_EQ(contextsPerWorker, contexts[workerIndex].size());
        for (const auto& context : contexts[workerIndex]) {
            EXPECT_TRUE(generations.insert(context.Generation()).second);
            EXPECT_EQ(10, context.DeviceId());
            EXPECT_EQ(20, context.CompanyId());
            if (context.Generation() > newest->Generation()) {
                newest = &context;
            }
        }
    }

    EXPECT_EQ(workerCount * contextsPerWorker + 1, generations.size());
    EXPECT_EQ(workerCount * contextsPerWorker + 1, newest->Generation());
    EXPECT_TRUE(manager.IsCurrentContext(*newest));
    for (const auto& row : contexts) {
        for (const auto& context : row) {
            if (!(context == *newest)) {
                EXPECT_FALSE(manager.EndContext(context));
            }
        }
    }
    EXPECT_TRUE(manager.IsCurrentContext(*newest));
}

}  // namespace sbp
