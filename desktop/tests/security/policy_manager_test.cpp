#include "security/policy_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
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

}  // namespace sbp
