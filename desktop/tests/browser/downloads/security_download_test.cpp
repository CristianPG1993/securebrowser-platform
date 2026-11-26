#include "download_test_client.h"
#include "browser/support/local_http_test_server.h"

#include "include/base/cef_callback.h"
#include "include/cef_file_util.h"
#include "include/wrapper/cef_closure_task.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace sbp {

// Batería SuggestedName / SecurityDownloadTest: autorización del nombre sugerido por el servidor.
// Comprueba Content-Disposition frente a URLs engañosas y redirecciones, Unicode, mayúsculas,
// doble extensión, ausencia de extensión y reglas activadas o desactivadas en ambos modos de lista.
// Exige el archivo completo si se permite y un único aviso sin diálogo ni archivo final si se bloquea.
struct SecurityDownloadCase {
    const char* name;
    const char* path;
    const char* suggestedName;
    DownloadTestTrigger trigger;
    // Sin motivo esperado, la descarga debe completarse; con motivo, debe bloquearse antes del diálogo.
    std::optional<SecurityReason> blockReason;
    const char* blockedExtension = "";
    DownloadControlConfig config{true, FilterMode::Denylist, {"exe"}};
};

// El hilo UI de CEF escribe estos datos; GoogleTest los consulta después de que Run confirme el cierre.
struct DownloadTestObservations {
    std::size_t destinationRequests = 0;
    std::optional<std::uint64_t> destinationOperationId;
    std::string requestedName;
    std::vector<DownloadDecisionNotice> notices;
    std::vector<std::uint64_t> closeRequests;
};

// El servidor y el destino se aíslan por caso; TearDown también se ejecuta tras una aserción fatal.
class DownloadTestBase : public ::testing::Test {
protected:
    void SetUp() override;
    void TearDown() override;

    CefRefPtr<LocalHttpTestServer> server_;
    CefString temporaryDirectory_;
    bool temporaryDirectoryCreated_ = false;
    std::filesystem::path destination_;
};

class SecurityDownloadTest : public DownloadTestBase,
                             public ::testing::WithParamInterface<SecurityDownloadCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<SecurityDownloadCase>& info);
};

std::string SecurityDownloadTest::CaseName(
    const ::testing::TestParamInfo<SecurityDownloadCase>& info) {
    return info.param.name;
}

void DownloadTestBase::SetUp() {
    // Las operaciones de archivos se realizan desde GoogleTest, fuera de los hilos UI e IO de CEF.
    temporaryDirectoryCreated_ = CefCreateNewTempDirectory("sbp-download-", temporaryDirectory_);
    ASSERT_TRUE(temporaryDirectoryCreated_);
    const std::filesystem::path directory(temporaryDirectory_.ToWString());
    ASSERT_TRUE(directory.is_absolute());
    destination_ = directory / L"saved.pdf";

    server_ = new LocalHttpTestServer();
    ASSERT_TRUE(server_->Start()) << "Could not bind the local download test server.";
}

void DownloadTestBase::TearDown() {
    if (server_) {
        server_->Stop();
        server_ = nullptr;
    }
    // Solo elimina el directorio nuevo que CEF creó para este caso, nunca una carpeta elegida por el usuario.
    if (temporaryDirectoryCreated_) {
        EXPECT_TRUE(CefDeleteFile(temporaryDirectory_, true)) << "Could not remove the download test directory.";
    }
}

// Exige callbacks nativos, decisión coherente, aviso único y un resultado comprobado en el archivo final.
TEST_P(SecurityDownloadTest, AppliesSuggestedNamePolicyAndChecksFinalFile) {
    const auto& testCase = GetParam();
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Download test policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        testCase.config, timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    const std::u8string utf8Destination = destination_.u8string();
    const std::string destinationPath(utf8Destination.begin(), utf8Destination.end());
    const auto observations = std::make_shared<DownloadTestObservations>();

    // Simula la respuesta del diálogo con una ruta propia; onResult mantiene el salto asíncrono a CompleteSaveDialog.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [observations, destinationPath](std::uint64_t operationId, std::string suggestedName,
                                       SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->destinationRequests;
            observations->destinationOperationId = operationId;
            observations->requestedName = std::move(suggestedName);
            onResult(destinationPath);
            return true;
        };
    // Conserva copias del aviso, sin transferir CefDownloadItem ni referencias a variables locales del test.
    SecurityDownloadHandler::DecisionCallback onDecision =
        [observations](const DownloadDecisionNotice& notice) {
            observations->notices.push_back(notice);
            return true;
        };
    CefRefPtr<SecurityDownloadHandler> handler = new SecurityDownloadHandler(
        manager, std::move(onRequestSaveDialog), {}, std::move(onDecision));
    CefRefPtr<DownloadTestClient> client = new DownloadTestClient(
        handler, server_->Origin() + "/download/page", server_->Origin() + testCase.path, testCase.trigger);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_FALSE(result.rejectedByCanDownload);
    if (testCase.trigger == DownloadTestTrigger::StartDownload) {
        EXPECT_EQ(0U, result.canDownloadCalls);
    } else {
        EXPECT_GT(result.canDownloadCalls, 0U);
    }
    ASSERT_EQ(1U, result.beforeDownloadCalls);
    EXPECT_GT(result.downloadUpdateCalls, 0U);
    EXPECT_TRUE(result.downloadId.has_value());
    EXPECT_EQ(testCase.suggestedName, result.suggestedName);
    EXPECT_GT(server_->RequestCount(testCase.path), 0U);

    std::error_code error;
    const bool finalFileExists = std::filesystem::exists(destination_, error);
    ASSERT_FALSE(error) << error.message();
    if (!testCase.blockReason.has_value()) {
        ASSERT_TRUE(result.completed);
        EXPECT_FALSE(result.canceled);
        EXPECT_FALSE(result.interrupted);
        EXPECT_EQ(1U, observations->destinationRequests);
        EXPECT_EQ(testCase.suggestedName, observations->requestedName);
        EXPECT_TRUE(observations->notices.empty());
        ASSERT_TRUE(finalFileExists);
        EXPECT_EQ(destination_, std::filesystem::path(CefString(result.fullPath).ToWString()));

        // Una descarga permitida solo cuenta como correcta si llegó el contenido completo del servidor.
        std::ifstream file(destination_, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::string expected = "SBP download fixture content.\n";
        EXPECT_EQ(expected, content);
        EXPECT_EQ(static_cast<std::int64_t>(expected.size()), result.receivedBytes);
    } else {
        EXPECT_FALSE(result.completed);
        EXPECT_TRUE(result.canceled);
        EXPECT_FALSE(finalFileExists);
        EXPECT_EQ(0U, observations->destinationRequests);
        ASSERT_EQ(1U, observations->notices.size());
        const auto& notice = observations->notices.front();
        EXPECT_GT(notice.operationId, 0U);
        EXPECT_GT(notice.browserId, 0);
        EXPECT_EQ(result.downloadId, notice.downloadId);
        EXPECT_EQ(*testCase.blockReason, notice.decision.Reason());
        EXPECT_EQ(testCase.blockedExtension, notice.extension);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(1, *notice.policyId);
    }
}

INSTANTIATE_TEST_SUITE_P(
    SuggestedName,
    SecurityDownloadTest,
    ::testing::Values(
        // El enlace termina en .exe, pero Content-Disposition sugiere un PDF que debe completarse.
        SecurityDownloadCase{"LinkAllowsPdfDespiteExeUrl", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::Link, std::nullopt},
        // La URL aparenta un PDF; el nombre programa.exe debe bloquearse antes de pedir destino.
        SecurityDownloadCase{"LinkBlocksExeDespitePdfUrl", "/download/blocked.pdf", "programa.exe",
            DownloadTestTrigger::Link, SecurityReason::ExtensionDenied, "exe"},
        // StartDownload omite CanDownload, pero conserva la autorización y el archivo permitido completo.
        SecurityDownloadCase{"StartDownloadAllowsPdfDespiteExeUrl", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::StartDownload, std::nullopt},
        // El nombre bloqueado tampoco puede continuar por la entrada StartDownload.
        SecurityDownloadCase{"StartDownloadBlocksExeDespitePdfUrl", "/download/blocked.pdf", "programa.exe",
            DownloadTestTrigger::StartDownload, SecurityReason::ExtensionDenied, "exe"},
        // Conserva la ñ de filename* y permite PDF en mayúsculas al compararlo con la regla pdf.
        SecurityDownloadCase{"LinkAllowsUnicodeNameAndUppercaseExtension", "/download/unicode",
            "informe-ni\xC3\xB1o.PDF", DownloadTestTrigger::Link, std::nullopt, "",
            DownloadControlConfig(true, FilterMode::Allowlist, {"pdf"})},
        // La comparación Unicode también debe relacionar ÉXE con la regla éxe sin alterar el nombre.
        SecurityDownloadCase{"StartDownloadBlocksUnicodeExtensionIgnoringCase", "/download/unicode-extension.pdf",
            "informe.\xC3\x89XE", DownloadTestTrigger::StartDownload, SecurityReason::ExtensionDenied, "\xC3\xA9xe",
            DownloadControlConfig(true, FilterMode::Denylist, {"\xC3\xA9xe"})},
        // En una doble extensión manda la última: informe.pdf.exe sigue siendo exe.
        SecurityDownloadCase{"LinkBlocksLastExtensionInDoubleName", "/download/double.pdf", "informe.pdf.exe",
            DownloadTestTrigger::Link, SecurityReason::ExtensionDenied, "exe"},
        // Con el control activo, un nombre sin punto no ofrece una extensión que se pueda autorizar.
        SecurityDownloadCase{"LinkBlocksMissingExtension", "/download/no-extension", "informe",
            DownloadTestTrigger::Link, SecurityReason::MissingExtension},
        // Desactivar el control permite el nombre válido aunque no tenga extensión.
        SecurityDownloadCase{"LinkAllowsMissingExtensionWithControlDisabled", "/download/no-extension", "informe",
            DownloadTestTrigger::Link, std::nullopt, "",
            DownloadControlConfig(false, FilterMode::Denylist, {"exe"})},
        // CEF transforma .env en env antes del callback; sigue sin extensión y debe bloquearse.
        SecurityDownloadCase{"LinkBlocksDotfileWithoutExtension", "/download/dotfile", "env",
            DownloadTestTrigger::Link, SecurityReason::MissingExtension},
        // El nombre env que entrega CEF puede continuar sin extensión cuando el control está desactivado.
        SecurityDownloadCase{"LinkAllowsDotfileWithControlDisabled", "/download/dotfile", "env",
            DownloadTestTrigger::Link, std::nullopt, "",
            DownloadControlConfig(false, FilterMode::Denylist, {"exe"})},
        // Una lista de denegación vacía permite las extensiones válidas.
        SecurityDownloadCase{"LinkAllowsWithEmptyDenylist", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::Link, std::nullopt, "",
            DownloadControlConfig(true, FilterMode::Denylist, {})},
        // Una lista de permitidos vacía tampoco autoriza descargas iniciadas con StartDownload.
        SecurityDownloadCase{"StartDownloadBlocksWithEmptyAllowlist", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::StartDownload, SecurityReason::ExtensionNotAllowed, "pdf",
            DownloadControlConfig(true, FilterMode::Allowlist, {})},
        // La regla con punto inicial y mayúsculas debe autorizar el PDF sugerido por el servidor.
        SecurityDownloadCase{"LinkAllowsListedExtensionWithDotAndCase", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::Link, std::nullopt, "",
            DownloadControlConfig(true, FilterMode::Allowlist, {".PDF"})},
        // Una extensión ausente de la lista de permitidos se bloquea con su motivo específico.
        SecurityDownloadCase{"LinkBlocksUnlistedExtension", "/download/blocked.pdf", "programa.exe",
            DownloadTestTrigger::Link, SecurityReason::ExtensionNotAllowed, "exe",
            DownloadControlConfig(true, FilterMode::Allowlist, {"pdf"})},
        // Desactivar el control ignora incluso una regla que coincide con la extensión sugerida y la final.
        SecurityDownloadCase{"StartDownloadIgnoresMatchingDenyRuleWithControlDisabled", "/download/allowed.exe",
            "informe.pdf", DownloadTestTrigger::StartDownload, std::nullopt, "",
            DownloadControlConfig(false, FilterMode::Denylist, {"pdf"})},
        // Una redirección desde una URL .exe conserva el nombre PDF de la respuesta final permitida.
        SecurityDownloadCase{"LinkAllowsFinalNameAfterRedirect", "/download/redirect.exe", "informe.pdf",
            DownloadTestTrigger::Link, std::nullopt},
        // La redirección inversa no permite ocultar el programa.exe de la respuesta final.
        SecurityDownloadCase{"StartDownloadBlocksFinalNameAfterRedirect", "/download/redirect.pdf", "programa.exe",
            DownloadTestTrigger::StartDownload, SecurityReason::ExtensionDenied, "exe"}),
    SecurityDownloadTest::CaseName);

// Batería FinalDestination / DownloadDestinationTest: autorización del destino elegido y respuesta única.
// Comprueba renombrados permitidos o bloqueados, nombres Windows inválidos y cancelación voluntaria.
// Las respuestas posteriores no deben reactivar, cancelar ni cambiar el destino ya resuelto.
// Después de responder una vez, simula otra respuesta para la misma operación del diálogo.
enum class DownloadDialogLateReply { None, Destination, Cancel };

// nullopt en blockReason distingue permisos y cancelaciones voluntarias de los bloqueos del evaluador.
// finalName == nullptr representa Cancelar en el diálogo; en los demás casos se devuelve una ruta absoluta.
struct DownloadDestinationCase {
    const char* name;
    const char* finalName;
    DownloadTestTrigger trigger;
    std::optional<SecurityReason> blockReason;
    DownloadDialogLateReply lateReply = DownloadDialogLateReply::None;
};

class DownloadDestinationTest : public DownloadTestBase,
                                public ::testing::WithParamInterface<DownloadDestinationCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<DownloadDestinationCase>& info);
};

std::string DownloadDestinationTest::CaseName(
    const ::testing::TestParamInfo<DownloadDestinationCase>& info) {
    return info.param.name;
}

// El nombre sugerido siempre está permitido; la respuesta al diálogo es la que debe decidir la continuación.
TEST_P(DownloadDestinationTest, RechecksChosenDestinationAndDistinguishesUserCancellation) {
    const auto& testCase = GetParam();
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Download destination test policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    std::optional<std::string> selectedPath;
    if (testCase.finalName != nullptr) {
        // Construye la ruta sin abrir el archivo, incluso cuando su nombre Windows es inválido.
        destination_ = std::filesystem::path(temporaryDirectory_.ToWString())
            / std::filesystem::path(CefString(testCase.finalName).ToWString());
        const std::u8string utf8Destination = destination_.u8string();
        selectedPath = std::string(utf8Destination.begin(), utf8Destination.end());
    }
    const auto observations = std::make_shared<DownloadTestObservations>();

    // El segundo destino siempre es distinto del primero y no debe llegar a crearse.
    const auto lateDestination = std::filesystem::path(temporaryDirectory_.ToWString()) / L"late.pdf";
    const std::u8string utf8LateDestination = lateDestination.u8string();
    std::vector<std::optional<std::string>> replies{selectedPath};
    if (testCase.lateReply == DownloadDialogLateReply::Destination) {
        replies.emplace_back(std::string(utf8LateDestination.begin(), utf8LateDestination.end()));
    } else if (testCase.lateReply == DownloadDialogLateReply::Cancel) {
        replies.emplace_back(std::nullopt);
    }

    // Cada respuesta encola su propia CompleteSaveDialog; las posteriores deben encontrar retirada la solicitud.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [observations, replies](std::uint64_t operationId, std::string suggestedName,
                                     SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->destinationRequests;
            observations->destinationOperationId = operationId;
            observations->requestedName = std::move(suggestedName);
            for (const auto& reply : replies) {
                onResult(reply);
            }
            return true;
        };
    // Una cancelación retira el diálogo una sola vez; una confirmación válida ya consume su solicitud.
    SecurityDownloadHandler::RequestCloseSaveDialogCallback onRequestCloseSaveDialog =
        [observations](std::uint64_t operationId) {
            observations->closeRequests.push_back(operationId);
        };
    // Solo los bloqueos del destino definitivo deben llegar aquí; cancelar el diálogo no genera aviso.
    SecurityDownloadHandler::DecisionCallback onDecision =
        [observations](const DownloadDecisionNotice& notice) {
            observations->notices.push_back(notice);
            return true;
        };
    CefRefPtr<SecurityDownloadHandler> handler = new SecurityDownloadHandler(
        manager, std::move(onRequestSaveDialog), std::move(onRequestCloseSaveDialog), std::move(onDecision));
    CefRefPtr<DownloadTestClient> client = new DownloadTestClient(
        handler, server_->Origin() + "/download/page", server_->Origin() + "/download/allowed.exe",
        testCase.trigger);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_FALSE(result.rejectedByCanDownload);
    if (testCase.trigger == DownloadTestTrigger::StartDownload) {
        EXPECT_EQ(0U, result.canDownloadCalls);
    } else {
        EXPECT_GT(result.canDownloadCalls, 0U);
    }
    EXPECT_EQ(1U, result.beforeDownloadCalls);
    EXPECT_GT(result.downloadUpdateCalls, 0U);
    EXPECT_TRUE(result.downloadId.has_value());
    EXPECT_EQ("informe.pdf", result.suggestedName);
    ASSERT_EQ(1U, observations->destinationRequests);
    EXPECT_EQ("informe.pdf", observations->requestedName);
    ASSERT_TRUE(observations->destinationOperationId.has_value());
    EXPECT_GT(*observations->destinationOperationId, 0U);

    std::error_code lateDestinationError;
    const bool lateFileExists = std::filesystem::exists(lateDestination, lateDestinationError);
    ASSERT_FALSE(lateDestinationError) << lateDestinationError.message();
    EXPECT_FALSE(lateFileExists);
    const bool shouldComplete = testCase.finalName != nullptr && !testCase.blockReason.has_value();
    ASSERT_EQ(shouldComplete ? 0U : 1U, observations->closeRequests.size());
    if (!shouldComplete) {
        EXPECT_EQ(*observations->destinationOperationId, observations->closeRequests.front());
    }
    if (shouldComplete) {
        ASSERT_TRUE(result.completed);
        EXPECT_FALSE(result.canceled);
        EXPECT_FALSE(result.interrupted);
        EXPECT_TRUE(observations->notices.empty());
        EXPECT_EQ(destination_, std::filesystem::path(CefString(result.fullPath).ToWString()));
        std::ifstream file(destination_, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::string expected = "SBP download fixture content.\n";
        EXPECT_EQ(expected, content);
        EXPECT_EQ(static_cast<std::int64_t>(expected.size()), result.receivedBytes);
    } else {
        EXPECT_FALSE(result.completed);
        EXPECT_TRUE(result.canceled);
        // Inspecciona solo la carpeta propia, sin consultar NUL.txt como dispositivo ni afirmar cero temporales en CEF.
        std::error_code error;
        const bool destinationDirectoryEmpty = std::filesystem::is_empty(
            std::filesystem::path(temporaryDirectory_.ToWString()), error);
        ASSERT_FALSE(error) << error.message();
        EXPECT_TRUE(destinationDirectoryEmpty);
    }

    if (testCase.blockReason.has_value()) {
        ASSERT_EQ(1U, observations->notices.size());
        const auto& notice = observations->notices.front();
        EXPECT_EQ(*observations->destinationOperationId, notice.operationId);
        EXPECT_EQ(result.downloadId, notice.downloadId);
        EXPECT_EQ(*testCase.blockReason, notice.decision.Reason());
        // En estos casos se renombra un PDF a EXE o se elige un nombre inválido.
        EXPECT_EQ(*testCase.blockReason == SecurityReason::ExtensionDenied ? "exe" : "", notice.extension);
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(1, *notice.policyId);
    } else {
        EXPECT_TRUE(observations->notices.empty());
    }
}

INSTANTIATE_TEST_SUITE_P(
    FinalDestination,
    DownloadDestinationTest,
    ::testing::Values(
        // El destino Unicode conserva su nombre y la extensión PDF permitida al confirmar el enlace.
        DownloadDestinationCase{"LinkAllowsRenamedUnicodePdf", "resumen-ni\xC3\xB1o.PDF",
            DownloadTestTrigger::Link, std::nullopt},
        // Cambiar informe.pdf a programa.exe debe bloquear la continuación después de pedir el destino.
        DownloadDestinationCase{"LinkBlocksRenamedExe", "programa.exe",
            DownloadTestTrigger::Link, SecurityReason::ExtensionDenied},
        // Un nombre reservado de Windows se rechaza antes de entregar la ruta a CEF.
        DownloadDestinationCase{"LinkBlocksReservedFinalName", "NUL.txt",
            DownloadTestTrigger::Link, SecurityReason::InvalidFileName},
        // Cancelar el diálogo termina la descarga sin archivo final ni aviso de bloqueo.
        DownloadDestinationCase{"LinkUserCancelsDialog", nullptr,
            DownloadTestTrigger::Link, std::nullopt},
        // StartDownload también debe conservar el destino Unicode aprobado por la comprobación final.
        DownloadDestinationCase{"StartDownloadAllowsRenamedUnicodePdf", "resumen-ni\xC3\xB1o.PDF",
            DownloadTestTrigger::StartDownload, std::nullopt},
        // Omitir CanDownload no evita la reevaluación de la extensión elegida al confirmar.
        DownloadDestinationCase{"StartDownloadBlocksRenamedExe", "programa.exe",
            DownloadTestTrigger::StartDownload, SecurityReason::ExtensionDenied},
        // El nombre reservado sigue siendo inválido con la entrada programática de CEF.
        DownloadDestinationCase{"StartDownloadBlocksReservedFinalName", "NUL.txt",
            DownloadTestTrigger::StartDownload, SecurityReason::InvalidFileName},
        // La cancelación voluntaria tampoco se convierte en bloqueo al comenzar con StartDownload.
        DownloadDestinationCase{"StartDownloadUserCancelsDialog", nullptr,
            DownloadTestTrigger::StartDownload, std::nullopt},
        // Cancelar retira la petición: una ruta recibida después no puede reanudarla ni crear late.pdf.
        DownloadDestinationCase{"LinkIgnoresDestinationAfterCancellation", nullptr,
            DownloadTestTrigger::Link, std::nullopt, DownloadDialogLateReply::Destination},
        // La confirmación consume la petición; una cancelación posterior no debe cancelar la transferencia autorizada.
        DownloadDestinationCase{"LinkIgnoresCancellationAfterConfirmation", "saved.pdf",
            DownloadTestTrigger::Link, std::nullopt, DownloadDialogLateReply::Cancel},
        // Una segunda confirmación con otro destino no puede modificar la ruta ya autorizada.
        DownloadDestinationCase{"LinkIgnoresSecondDestinationAfterConfirmation", "saved.pdf",
            DownloadTestTrigger::Link, std::nullopt, DownloadDialogLateReply::Destination},
        // La respuesta tardía tampoco revive una cancelación cuando se utilizó StartDownload.
        DownloadDestinationCase{"StartDownloadIgnoresDestinationAfterCancellation", nullptr,
            DownloadTestTrigger::StartDownload, std::nullopt, DownloadDialogLateReply::Destination},
        // StartDownload conserva la descarga autorizada aunque llegue una cancelación de diálogo repetida.
        DownloadDestinationCase{"StartDownloadIgnoresCancellationAfterConfirmation", "saved.pdf",
            DownloadTestTrigger::StartDownload, std::nullopt, DownloadDialogLateReply::Cancel},
        // El destino de la primera respuesta también es definitivo con la entrada programática.
        DownloadDestinationCase{"StartDownloadIgnoresSecondDestinationAfterConfirmation", "saved.pdf",
            DownloadTestTrigger::StartDownload, std::nullopt, DownloadDialogLateReply::Destination}),
    DownloadDestinationTest::CaseName);

// Batería PendingDialog / DownloadPolicyChangeTest: política y vigencia al confirmar el destino.
// Comprueba que se utilicen las reglas nuevas publicadas mientras se esperaba la respuesta del diálogo.
// Cubre el instante anterior a la caducidad, el límite exacto de 15 minutos y la vigencia ilimitada.
// La política inicial permite informe.pdf al abrir el diálogo; la nueva decide sobre el destino elegido.
struct DownloadPolicyChangeCase {
    const char* name;
    DownloadTestTrigger trigger;
    const char* finalName;
    DownloadControlConfig initialConfig;
    DownloadControlConfig updatedConfig;
    std::optional<SecurityReason> blockReason;
    // El reloj avanza después de publicar la política y antes de responder al diálogo.
    std::chrono::seconds elapsedBeforeReply{0};
    PolicyManager::Validity validity = PolicyManager::Validity::FifteenMinutes;
};

// Solo UI CEF modifica estas observaciones; Run sincroniza su lectura posterior desde GoogleTest.
struct DownloadPolicyChangeObservations {
    DownloadTestObservations download;
    bool responseQueued = false;
    std::optional<PolicyPublicationResult> publicationResult;
};

class DownloadPolicyChangeTest : public DownloadTestBase,
                                 public ::testing::WithParamInterface<DownloadPolicyChangeCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<DownloadPolicyChangeCase>& info);
};

std::string DownloadPolicyChangeTest::CaseName(
    const ::testing::TestParamInfo<DownloadPolicyChangeCase>& info) {
    return info.param.name;
}

// La confirmación consulta la política nueva y su vigencia, conservando el contexto de la petición.
TEST_P(DownloadPolicyChangeTest, UsesLatestPolicyWhenPendingDialogReturnsDestination) {
    const auto& testCase = GetParam();
    // El contador es compartido y atómico porque el gestor puede consultar el reloj desde distintos hilos.
    const auto clockSeconds = std::make_shared<std::atomic<std::int64_t>>(0);
    // Devuelve un instante monotónico simulado; la vigencia no depende del tiempo real que tarde CEF.
    const auto manager = std::make_shared<PolicyManager>([clockSeconds] {
        return PolicyManager::Clock::time_point{std::chrono::duration_cast<PolicyManager::Clock::duration>(
            std::chrono::seconds{clockSeconds->load()})};
    }, testCase.validity);
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot initialPolicy(
        1, 20, "Initial download test policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        testCase.initialConfig, timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, initialPolicy));
    const PolicySnapshot updatedPolicy(
        2, 20, "Updated download test policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        testCase.updatedConfig, timestamp, timestamp);

    destination_ = std::filesystem::path(temporaryDirectory_.ToWString())
        / std::filesystem::path(CefString(testCase.finalName).ToWString());
    const std::u8string utf8Destination = destination_.u8string();
    const std::string destinationPath(utf8Destination.begin(), utf8Destination.end());
    const auto observations = std::make_shared<DownloadPolicyChangeObservations>();

    // Conserva la respuesta hasta otra tarea UI CEF, sin capturar el test ni usar esperas temporizadas.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [manager, context = *context, updatedPolicy, observations, destinationPath, clockSeconds,
         elapsedBeforeReply = testCase.elapsedBeforeReply](
            std::uint64_t operationId, std::string suggestedName,
            SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->download.destinationRequests;
            observations->download.destinationOperationId = operationId;
            observations->download.requestedName = std::move(suggestedName);

            // BindOnce conserva los argumentos por valor; CEF exige que esta lambda no tenga capturas.
            const bool queued = CefPostTask(TID_UI, base::BindOnce(
                [](std::shared_ptr<PolicyManager> policyManager, SecurityContext originalContext,
                   PolicySnapshot policy, std::shared_ptr<DownloadPolicyChangeObservations> state,
                   std::string path, SecurityDownloadHandler::SaveDialogResultCallback reply,
                   std::shared_ptr<std::atomic<std::int64_t>> elapsedSeconds, std::chrono::seconds elapsed) {
                    state->publicationResult = policyManager->PublishPolicy(originalContext, std::move(policy));
                    if (*state->publicationResult != PolicyPublicationResult::Published) {
                        reply(std::nullopt);
                        return;
                    }
                    // La política se validó en el instante cero; avanzar el reloj no renueva su vigencia.
                    elapsedSeconds->store(elapsed.count());
                    reply(std::move(path));
                }, manager, context, updatedPolicy, observations, destinationPath, onResult,
                   clockSeconds, elapsedBeforeReply));
            observations->responseQueued = queued;
            if (!queued) {
                onResult(std::nullopt);
            }
            return queued;
        };
    // El bloqueo debe conservar un único aviso con la política nueva y el contexto original.
    SecurityDownloadHandler::DecisionCallback onDecision =
        [observations](const DownloadDecisionNotice& notice) {
            observations->download.notices.push_back(notice);
            return true;
        };
    CefRefPtr<SecurityDownloadHandler> handler = new SecurityDownloadHandler(
        manager, std::move(onRequestSaveDialog), {}, std::move(onDecision));
    CefRefPtr<DownloadTestClient> client = new DownloadTestClient(
        handler, server_->Origin() + "/download/page", server_->Origin() + "/download/allowed.exe",
        testCase.trigger);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_FALSE(result.rejectedByCanDownload);
    if (testCase.trigger == DownloadTestTrigger::StartDownload) {
        EXPECT_EQ(0U, result.canDownloadCalls);
    } else {
        EXPECT_GT(result.canDownloadCalls, 0U);
    }
    EXPECT_EQ(1U, result.beforeDownloadCalls);
    EXPECT_GT(result.downloadUpdateCalls, 0U);
    ASSERT_TRUE(result.downloadId.has_value());
    EXPECT_EQ("informe.pdf", result.suggestedName);
    ASSERT_EQ(1U, observations->download.destinationRequests);
    EXPECT_EQ("informe.pdf", observations->download.requestedName);
    ASSERT_TRUE(observations->download.destinationOperationId.has_value());
    EXPECT_GT(*observations->download.destinationOperationId, 0U);
    ASSERT_TRUE(observations->responseQueued);
    ASSERT_TRUE(observations->publicationResult.has_value());
    ASSERT_EQ(PolicyPublicationResult::Published, *observations->publicationResult);
    const auto state = manager->ReadState();
    ASSERT_TRUE(state);
    EXPECT_EQ(*context, state->context);
    ASSERT_TRUE(state->policy);
    EXPECT_EQ(2, state->policy->Id());
    ASSERT_TRUE(state->validatedAt.has_value());
    EXPECT_EQ(PolicyManager::Clock::time_point{}, *state->validatedAt);
    const auto expectedAvailability = testCase.blockReason == SecurityReason::PolicyExpired
        ? PolicyAvailability::Expired : PolicyAvailability::Available;
    EXPECT_EQ(expectedAvailability, manager->ReadPolicy(*context).availability);

    std::error_code error;
    const bool finalFileExists = std::filesystem::exists(destination_, error);
    ASSERT_FALSE(error) << error.message();
    if (testCase.blockReason.has_value()) {
        EXPECT_FALSE(result.completed);
        EXPECT_TRUE(result.canceled);
        EXPECT_FALSE(finalFileExists);
        ASSERT_EQ(1U, observations->download.notices.size());
        const auto& notice = observations->download.notices.front();
        EXPECT_EQ(*observations->download.destinationOperationId, notice.operationId);
        EXPECT_GT(notice.browserId, 0);
        EXPECT_EQ(result.downloadId, notice.downloadId);
        EXPECT_EQ(*testCase.blockReason, notice.decision.Reason());
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(2, *notice.policyId);
    } else {
        ASSERT_TRUE(result.completed);
        EXPECT_FALSE(result.canceled);
        EXPECT_FALSE(result.interrupted);
        EXPECT_TRUE(observations->download.notices.empty());
        ASSERT_TRUE(finalFileExists);
        EXPECT_EQ(destination_, std::filesystem::path(CefString(result.fullPath).ToWString()));
        std::ifstream file(destination_, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        const std::string expected = "SBP download fixture content.\n";
        EXPECT_EQ(expected, content);
        EXPECT_EQ(static_cast<std::int64_t>(expected.size()), result.receivedBytes);
    }
}

INSTANTIATE_TEST_SUITE_P(
    PendingDialog,
    DownloadPolicyChangeTest,
    ::testing::Values(
        // El PDF inicial estaba permitido; publicar una denegación de pdf debe bloquear el destino final.
        DownloadPolicyChangeCase{"LinkBlocksDestinationAfterPolicyUpdate", DownloadTestTrigger::Link, "saved.pdf",
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"pdf"}), SecurityReason::ExtensionDenied},
        // El destino txt estaba bloqueado; quitar esa regla debe permitirlo al confirmar el diálogo.
        DownloadPolicyChangeCase{"LinkAllowsDestinationAfterPolicyUpdate", DownloadTestTrigger::Link, "saved.txt",
            DownloadControlConfig(true, FilterMode::Denylist, {"txt"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt},
        // StartDownload también debe utilizar la nueva denegación antes de entregar la continuación a CEF.
        DownloadPolicyChangeCase{"StartDownloadBlocksDestinationAfterPolicyUpdate", DownloadTestTrigger::StartDownload,
            "saved.pdf", DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"pdf"}), SecurityReason::ExtensionDenied},
        // Omitir CanDownload tampoco impide aplicar una política nueva que autoriza el destino elegido.
        DownloadPolicyChangeCase{"StartDownloadAllowsDestinationAfterPolicyUpdate", DownloadTestTrigger::StartDownload,
            "saved.txt", DownloadControlConfig(true, FilterMode::Denylist, {"txt"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt},
        // Un segundo antes del límite, el PDF elegido todavía puede autorizarse.
        DownloadPolicyChangeCase{"LinkAllowsBeforePolicyExpiry", DownloadTestTrigger::Link, "saved.pdf",
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt,
            std::chrono::seconds{899}},
        // Al alcanzar exactamente 15 minutos, confirmar el diálogo debe cancelar por PolicyExpired.
        DownloadPolicyChangeCase{"LinkBlocksAtPolicyExpiry", DownloadTestTrigger::Link, "saved.pdf",
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), SecurityReason::PolicyExpired,
            std::chrono::minutes{15}},
        // La vigencia ilimitada utilizada por la demo permite confirmar incluso un día después.
        DownloadPolicyChangeCase{"LinkAllowsUnlimitedPolicyAfterOneDay", DownloadTestTrigger::Link, "saved.pdf",
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt,
            std::chrono::hours{24}, PolicyManager::Validity::Unlimited},
        // StartDownload conserva la autorización justo antes de que termine el plazo.
        DownloadPolicyChangeCase{"StartDownloadAllowsBeforePolicyExpiry", DownloadTestTrigger::StartDownload,
            "saved.pdf", DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt,
            std::chrono::seconds{899}},
        // Omitir CanDownload tampoco permite confirmar una política que acaba de caducar.
        DownloadPolicyChangeCase{"StartDownloadBlocksAtPolicyExpiry", DownloadTestTrigger::StartDownload,
            "saved.pdf", DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), SecurityReason::PolicyExpired,
            std::chrono::minutes{15}},
        // La excepción explícita de vigencia ilimitada también se mantiene con StartDownload.
        DownloadPolicyChangeCase{"StartDownloadAllowsUnlimitedPolicyAfterOneDay", DownloadTestTrigger::StartDownload,
            "saved.pdf", DownloadControlConfig(true, FilterMode::Denylist, {"exe"}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), std::nullopt,
            std::chrono::hours{24}, PolicyManager::Validity::Unlimited}),
    DownloadPolicyChangeTest::CaseName);

// Batería PendingContext / DownloadContextChangeTest: conservación del contexto original de la descarga.
// Comprueba cierre, reinicio con los mismos IDs y cambio de instalación mientras el diálogo está pendiente.
// Exige cancelación por InvalidContext y cierre del diálogo, aunque el contexto nuevo tenga una política válida.
// La respuesta se retiene hasta la petición de cierre: retirar el contexto no debe esperar al usuario.
// Reiniciar la misma instalación también crea otra generación de contexto.
enum class DownloadContextChange { EndContext, RestartSameInstallation, SwitchInstallation };

struct DownloadContextChangeCase {
    const char* name;
    DownloadTestTrigger trigger;
    DownloadContextChange change;
};

// Las copias permiten comprobar el contexto nuevo sin trasladar la descarga a él.
struct DownloadContextChangeObservations {
    DownloadTestObservations download;
    bool contextChangeQueued = false;
    bool contextEnded = false;
    bool replySent = false;
    bool closeRequestedBeforeReply = false;
    SecurityDownloadHandler::SaveDialogResultCallback pendingReply;
    std::optional<SecurityContext> replacementContext;
    std::optional<PolicyPublicationResult> publicationResult;
    std::vector<SecurityContext> retiredContexts;
    std::vector<std::uint64_t> closeRequests;
};

class DownloadContextChangeTest : public DownloadTestBase,
                                  public ::testing::WithParamInterface<DownloadContextChangeCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<DownloadContextChangeCase>& info);
};

std::string DownloadContextChangeTest::CaseName(
    const ::testing::TestParamInfo<DownloadContextChangeCase>& info) {
    return info.param.name;
}

// La retirada cancela sin respuesta del diálogo; la respuesta tardía tampoco puede autorizar otro contexto.
TEST_P(DownloadContextChangeTest, CancelsOriginalDownloadWhenPendingDialogOutlivesContext) {
    const auto& testCase = GetParam();
    const auto manager = std::make_shared<PolicyManager>();
    const auto context = manager->BeginContext(10, 20);
    ASSERT_TRUE(context.has_value());
    const PolicySnapshot::Timestamp timestamp{};
    const PolicySnapshot policy(
        1, 20, "Original download context policy",
        UrlFilteringConfig(false, FilterMode::Denylist, {}),
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), timestamp, timestamp);
    ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));

    const std::u8string utf8Destination = destination_.u8string();
    const std::string destinationPath(utf8Destination.begin(), utf8Destination.end());
    const auto observations = std::make_shared<DownloadContextChangeObservations>();

    // La tarea conserva la respuesta y retira el contexto después de que se haya solicitado el diálogo.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [manager, context = *context, observations, change = testCase.change](
            std::uint64_t operationId, std::string suggestedName,
            SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->download.destinationRequests;
            observations->download.destinationOperationId = operationId;
            observations->download.requestedName = std::move(suggestedName);
            // Se responderá desde la petición de cierre, después de que el handler haya decidido cancelar.
            observations->pendingReply = std::move(onResult);

            // BindOnce recibe los datos por valor; ningún callback conserva referencias al test.
            const bool queued = CefPostTask(TID_UI, base::BindOnce(
                [](std::shared_ptr<PolicyManager> policyManager, SecurityContext originalContext,
                   std::shared_ptr<DownloadContextChangeObservations> state, DownloadContextChange change) {
                    state->contextEnded = policyManager->EndContext(originalContext);
                    if (!state->contextEnded) {
                        return;
                    }
                    if (change != DownloadContextChange::EndContext) {
                        const bool sameInstallation = change == DownloadContextChange::RestartSameInstallation;
                        state->replacementContext = policyManager->BeginContext(
                            sameInstallation ? 10 : 11, sameInstallation ? 20 : 21);
                        if (!state->replacementContext.has_value()) {
                            return;
                        }

                        // El contexto nuevo tiene una política válida que permitiría el PDF, pero no su origen antiguo.
                        const PolicySnapshot::Timestamp timestamp{};
                        const PolicySnapshot replacementPolicy(
                            2, state->replacementContext->CompanyId(), "Replacement download context policy",
                            UrlFilteringConfig(false, FilterMode::Denylist, {}),
                            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), timestamp, timestamp);
                        state->publicationResult = policyManager->PublishPolicy(
                            *state->replacementContext, replacementPolicy);
                    }
                    // No entrega destino: la suscripción debe solicitar la cancelación por sí misma.
                }, manager, context, observations, change));
            observations->contextChangeQueued = queued;
            return queued;
        };
    // Registra la petición real de retirar el diálogo, identificada con la operación original.
    SecurityDownloadHandler::RequestCloseSaveDialogCallback onRequestCloseSaveDialog =
        [observations, destinationPath](std::uint64_t operationId) {
            observations->closeRequests.push_back(operationId);
            observations->closeRequestedBeforeReply = !observations->replySent;
            if (observations->pendingReply) {
                auto reply = std::exchange(observations->pendingReply, {});
                observations->replySent = true;
                // Simula una respuesta tardía con ruta válida: la solicitud ya retirada debe ignorarla.
                reply(destinationPath);
            }
        };
    // El contexto nuevo no debe aparecer en el aviso de la descarga original ni causar avisos duplicados.
    SecurityDownloadHandler::DecisionCallback onDecision =
        [observations](const DownloadDecisionNotice& notice) {
            observations->download.notices.push_back(notice);
            return true;
        };
    CefRefPtr<SecurityDownloadHandler> handler = new SecurityDownloadHandler(
        manager, std::move(onRequestSaveDialog), std::move(onRequestCloseSaveDialog), std::move(onDecision));
    // Registra la entrega real antes de crear el navegador y conserva el receptor de producción.
    auto contextRetiredSubscription = manager->SubscribeToContextRetirement(
        [observations, onContextRetired = handler->MakeContextRetiredHandler()](const SecurityContext& retired) {
            observations->retiredContexts.push_back(retired);
            onContextRetired(retired);
        });
    CefRefPtr<DownloadTestClient> client = new DownloadTestClient(
        handler, server_->Origin() + "/download/page", server_->Origin() + "/download/allowed.exe",
        testCase.trigger);
    const auto result = client->Run();
    contextRetiredSubscription.reset();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_FALSE(result.rejectedByCanDownload);
    if (testCase.trigger == DownloadTestTrigger::StartDownload) {
        EXPECT_EQ(0U, result.canDownloadCalls);
    } else {
        EXPECT_GT(result.canDownloadCalls, 0U);
    }
    EXPECT_EQ(1U, result.beforeDownloadCalls);
    EXPECT_GT(result.downloadUpdateCalls, 0U);
    ASSERT_TRUE(result.downloadId.has_value());
    EXPECT_EQ("informe.pdf", result.suggestedName);
    ASSERT_EQ(1U, observations->download.destinationRequests);
    EXPECT_EQ("informe.pdf", observations->download.requestedName);
    ASSERT_TRUE(observations->download.destinationOperationId.has_value());
    EXPECT_GT(*observations->download.destinationOperationId, 0U);
    ASSERT_TRUE(observations->contextChangeQueued);
    ASSERT_TRUE(observations->contextEnded);
    ASSERT_EQ(1U, observations->retiredContexts.size());
    EXPECT_EQ(*context, observations->retiredContexts.front());
    EXPECT_TRUE(observations->closeRequestedBeforeReply);
    EXPECT_TRUE(observations->replySent);
    EXPECT_FALSE(observations->pendingReply);
    EXPECT_FALSE(manager->IsCurrentContext(*context));
    EXPECT_EQ(PolicyAvailability::InvalidContext, manager->ReadPolicy(*context).availability);

    const auto state = manager->ReadState();
    if (testCase.change == DownloadContextChange::EndContext) {
        EXPECT_FALSE(state);
        EXPECT_FALSE(observations->replacementContext.has_value());
        EXPECT_FALSE(observations->publicationResult.has_value());
    } else {
        ASSERT_TRUE(observations->replacementContext.has_value());
        EXPECT_NE(*context, *observations->replacementContext);
        ASSERT_TRUE(observations->publicationResult.has_value());
        EXPECT_EQ(PolicyPublicationResult::Published, *observations->publicationResult);
        ASSERT_TRUE(state);
        EXPECT_EQ(*observations->replacementContext, state->context);
        const bool sameInstallation = testCase.change == DownloadContextChange::RestartSameInstallation;
        EXPECT_EQ(sameInstallation ? 10 : 11, state->context.DeviceId());
        EXPECT_EQ(sameInstallation ? 20 : 21, state->context.CompanyId());
        ASSERT_TRUE(state->policy);
        EXPECT_EQ(2, state->policy->Id());
        EXPECT_EQ(PolicyAvailability::Available, manager->ReadPolicy(state->context).availability);
    }

    EXPECT_FALSE(result.completed);
    EXPECT_TRUE(result.canceled);
    std::error_code error;
    const bool finalFileExists = std::filesystem::exists(destination_, error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_FALSE(finalFileExists);
    ASSERT_EQ(1U, observations->closeRequests.size());
    EXPECT_EQ(*observations->download.destinationOperationId, observations->closeRequests.front());
    ASSERT_EQ(1U, observations->download.notices.size());
    const auto& notice = observations->download.notices.front();
    EXPECT_EQ(*observations->download.destinationOperationId, notice.operationId);
    EXPECT_GT(notice.browserId, 0);
    EXPECT_EQ(result.downloadId, notice.downloadId);
    EXPECT_EQ(SecurityReason::InvalidContext, notice.decision.Reason());
    ASSERT_TRUE(notice.context.has_value());
    EXPECT_EQ(*context, *notice.context);
    EXPECT_FALSE(notice.policyId.has_value());
}

INSTANTIATE_TEST_SUITE_P(
    PendingContext,
    DownloadContextChangeTest,
    ::testing::Values(
        // Cerrar el contexto durante el diálogo impide continuar la descarga del enlace.
        DownloadContextChangeCase{"LinkBlocksAfterContextEnds", DownloadTestTrigger::Link,
            DownloadContextChange::EndContext},
        // Los mismos IDs con otra generación no recuperan el permiso de la petición anterior.
        DownloadContextChangeCase{"LinkBlocksAfterSameInstallationRestarts", DownloadTestTrigger::Link,
            DownloadContextChange::RestartSameInstallation},
        // Una política válida de otra instalación tampoco puede autorizar el destino pendiente.
        DownloadContextChangeCase{"LinkBlocksAfterInstallationChanges", DownloadTestTrigger::Link,
            DownloadContextChange::SwitchInstallation},
        // StartDownload debe conservar la guarda de origen cuando se cierra el contexto.
        DownloadContextChangeCase{"StartDownloadBlocksAfterContextEnds", DownloadTestTrigger::StartDownload,
            DownloadContextChange::EndContext},
        // El reinicio de la misma instalación también invalida una solicitud iniciada por StartDownload.
        DownloadContextChangeCase{"StartDownloadBlocksAfterSameInstallationRestarts", DownloadTestTrigger::StartDownload,
            DownloadContextChange::RestartSameInstallation},
        // Cambiar de instalación no traslada la descarga programática a la nueva política.
        DownloadContextChangeCase{"StartDownloadBlocksAfterInstallationChanges", DownloadTestTrigger::StartDownload,
            DownloadContextChange::SwitchInstallation}),
    DownloadContextChangeTest::CaseName);

// Batería InitialAvailability / DownloadAvailabilityTest: disponibilidad antes de solicitar un destino.
// Comprueba enlaces sin contexto, sin política publicada o con política caducada desde CanDownload.
// Comprueba también que StartDownload rechace políticas ausentes o caducadas desde OnBeforeDownload.
// Ningún caso debe abrir el diálogo ni crear un archivo final; el aviso debe conservar el motivo correcto.
struct DownloadAvailabilityCase {
    const char* name;
    DownloadTestTrigger trigger;
    PolicyAvailability availability;
    SecurityReason blockReason;
};

class DownloadAvailabilityTest : public DownloadTestBase,
                                 public ::testing::WithParamInterface<DownloadAvailabilityCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<DownloadAvailabilityCase>& info);
};

std::string DownloadAvailabilityTest::CaseName(
    const ::testing::TestParamInfo<DownloadAvailabilityCase>& info) {
    return info.param.name;
}

// Una descarga no puede llegar al diálogo sin contexto y política vigentes, tampoco mediante StartDownload.
TEST_P(DownloadAvailabilityTest, BlocksBeforeRequestingDestinationWhenPolicyIsUnavailable) {
    const auto& testCase = GetParam();
    const auto clockSeconds = std::make_shared<std::atomic<std::int64_t>>(0);
    // Simula la caducidad antes de iniciar CEF, sin esperas reales ni cambios de hora del sistema.
    const auto manager = std::make_shared<PolicyManager>([clockSeconds] {
        return PolicyManager::Clock::time_point{std::chrono::duration_cast<PolicyManager::Clock::duration>(
            std::chrono::seconds{clockSeconds->load()})};
    });
    std::optional<SecurityContext> context;
    if (testCase.availability != PolicyAvailability::InvalidContext) {
        context = manager->BeginContext(10, 20);
        ASSERT_TRUE(context.has_value());
    }
    if (testCase.availability == PolicyAvailability::Expired) {
        const PolicySnapshot::Timestamp timestamp{};
        const PolicySnapshot policy(
            1, 20, "Expired initial download policy",
            UrlFilteringConfig(false, FilterMode::Denylist, {}),
            DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), timestamp, timestamp);
        ASSERT_EQ(PolicyPublicationResult::Published, manager->PublishPolicy(*context, policy));
        clockSeconds->store(900);
    }
    if (context.has_value()) {
        ASSERT_EQ(testCase.availability, manager->ReadPolicy(*context).availability);
    } else {
        ASSERT_FALSE(manager->ReadState());
    }

    const std::u8string utf8Destination = destination_.u8string();
    const std::string destinationPath(utf8Destination.begin(), utf8Destination.end());
    const auto observations = std::make_shared<DownloadTestObservations>();

    // Responde con un destino propio si se pidiera por error; el test exige que no se solicite ningún diálogo.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [observations, destinationPath](std::uint64_t operationId, std::string suggestedName,
                                       SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->destinationRequests;
            observations->destinationOperationId = operationId;
            observations->requestedName = std::move(suggestedName);
            onResult(destinationPath);
            return true;
        };
    // Conserva un único aviso con el motivo concreto, sin acceder al test desde el hilo UI de CEF.
    SecurityDownloadHandler::DecisionCallback onDecision =
        [observations](const DownloadDecisionNotice& notice) {
            observations->notices.push_back(notice);
            return true;
        };
    CefRefPtr<SecurityDownloadHandler> handler = new SecurityDownloadHandler(
        manager, std::move(onRequestSaveDialog), {}, std::move(onDecision));
    CefRefPtr<DownloadTestClient> client = new DownloadTestClient(
        handler, server_->Origin() + "/download/page", server_->Origin() + "/download/allowed.exe",
        testCase.trigger);
    const auto result = client->Run();

    ASSERT_TRUE(result.error.empty()) << result.error;
    ASSERT_TRUE(result.browserCreated);
    ASSERT_TRUE(result.browserClosed);
    EXPECT_FALSE(result.completed);
    EXPECT_EQ(0U, observations->destinationRequests);
    EXPECT_FALSE(observations->destinationOperationId.has_value());
    ASSERT_EQ(1U, observations->notices.size());
    const auto& notice = observations->notices.front();
    EXPECT_GT(notice.operationId, 0U);
    EXPECT_GT(notice.browserId, 0);
    EXPECT_EQ(testCase.blockReason, notice.decision.Reason());

    if (testCase.trigger == DownloadTestTrigger::Link) {
        EXPECT_GT(result.canDownloadCalls, 0U);
        EXPECT_TRUE(result.rejectedByCanDownload);
        EXPECT_EQ(0U, result.beforeDownloadCalls);
        // CanDownload no recibe un ID nativo; no inventamos un estado IsCanceled antes de existir la descarga.
        EXPECT_FALSE(notice.downloadId.has_value());
    } else {
        EXPECT_EQ(0U, result.canDownloadCalls);
        EXPECT_FALSE(result.rejectedByCanDownload);
        EXPECT_EQ(1U, result.beforeDownloadCalls);
        EXPECT_GT(result.downloadUpdateCalls, 0U);
        EXPECT_TRUE(result.canceled);
        ASSERT_TRUE(result.downloadId.has_value());
        EXPECT_EQ(result.downloadId, notice.downloadId);
    }
    if (context.has_value()) {
        ASSERT_TRUE(notice.context.has_value());
        EXPECT_EQ(*context, *notice.context);
    } else {
        EXPECT_FALSE(notice.context.has_value());
    }
    if (testCase.availability == PolicyAvailability::Expired) {
        ASSERT_TRUE(notice.policyId.has_value());
        EXPECT_EQ(1, *notice.policyId);
    } else {
        EXPECT_FALSE(notice.policyId.has_value());
    }

    std::error_code error;
    const bool destinationDirectoryEmpty = std::filesystem::is_empty(
        std::filesystem::path(temporaryDirectory_.ToWString()), error);
    ASSERT_FALSE(error) << error.message();
    EXPECT_TRUE(destinationDirectoryEmpty);
}

INSTANTIATE_TEST_SUITE_P(
    InitialAvailability,
    DownloadAvailabilityTest,
    ::testing::Values(
        // Sin sesión o instalación activa, el enlace debe rechazarse ya en CanDownload.
        DownloadAvailabilityCase{"LinkBlocksWithoutContext", DownloadTestTrigger::Link,
            PolicyAvailability::InvalidContext, SecurityReason::InvalidContext},
        // Un contexto creado todavía no permite descargar hasta recibir una política válida.
        DownloadAvailabilityCase{"LinkBlocksWithoutPublishedPolicy", DownloadTestTrigger::Link,
            PolicyAvailability::Unavailable, SecurityReason::PolicyUnavailable},
        // Si la política ya caducó antes de iniciar la petición, no se llega a solicitar un destino.
        DownloadAvailabilityCase{"LinkBlocksInitiallyExpiredPolicy", DownloadTestTrigger::Link,
            PolicyAvailability::Expired, SecurityReason::PolicyExpired},
        // StartDownload omite CanDownload, pero OnBeforeDownload debe bloquear la ausencia de política.
        DownloadAvailabilityCase{"StartDownloadBlocksWithoutPublishedPolicy", DownloadTestTrigger::StartDownload,
            PolicyAvailability::Unavailable, SecurityReason::PolicyUnavailable},
        // La entrada programática tampoco puede utilizar una política que ya estaba caducada.
        DownloadAvailabilityCase{"StartDownloadBlocksInitiallyExpiredPolicy", DownloadTestTrigger::StartDownload,
            PolicyAvailability::Expired, SecurityReason::PolicyExpired}),
    DownloadAvailabilityTest::CaseName);

}  // namespace sbp
