#include "download_test_client.h"
#include "browser/support/local_http_test_server.h"

#include "include/cef_file_util.h"

#include <gtest/gtest.h>

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

struct SecurityDownloadCase {
    const char* name;
    const char* path;
    const char* suggestedName;
    DownloadTestTrigger trigger;
    bool allowed;
};

// El hilo UI de CEF escribe estos datos; GoogleTest los consulta después de que Run confirme el cierre.
struct DownloadTestObservations {
    std::size_t destinationRequests = 0;
    std::optional<std::uint64_t> destinationOperationId;
    std::string requestedName;
    std::vector<DownloadDecisionNotice> notices;
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
        DownloadControlConfig(true, FilterMode::Denylist, {"exe"}), timestamp, timestamp);
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
    if (testCase.allowed) {
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
        EXPECT_EQ(SecurityReason::ExtensionDenied, notice.decision.Reason());
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
            DownloadTestTrigger::Link, true},
        // La URL aparenta un PDF; el nombre programa.exe debe bloquearse antes de pedir destino.
        SecurityDownloadCase{"LinkBlocksExeDespitePdfUrl", "/download/blocked.pdf", "programa.exe",
            DownloadTestTrigger::Link, false},
        // StartDownload omite CanDownload, pero conserva la autorización y el archivo permitido completo.
        SecurityDownloadCase{"StartDownloadAllowsPdfDespiteExeUrl", "/download/allowed.exe", "informe.pdf",
            DownloadTestTrigger::StartDownload, true},
        // El nombre bloqueado tampoco puede continuar por la entrada StartDownload.
        SecurityDownloadCase{"StartDownloadBlocksExeDespitePdfUrl", "/download/blocked.pdf", "programa.exe",
            DownloadTestTrigger::StartDownload, false}),
    SecurityDownloadTest::CaseName);

// nullopt en blockReason distingue permisos y cancelaciones voluntarias de los bloqueos del evaluador.
// finalName == nullptr representa Cancelar en el diálogo; en los demás casos se devuelve una ruta absoluta.
struct DownloadDestinationCase {
    const char* name;
    const char* finalName;
    DownloadTestTrigger trigger;
    std::optional<SecurityReason> blockReason;
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

    // Simula Guardar como o Cancelar; la respuesta real del handler vuelve a encolar CompleteSaveDialog en CEF.
    SecurityDownloadHandler::RequestSaveDialogCallback onRequestSaveDialog =
        [observations, selectedPath](std::uint64_t operationId, std::string suggestedName,
                                     SecurityDownloadHandler::SaveDialogResultCallback onResult) {
            ++observations->destinationRequests;
            observations->destinationOperationId = operationId;
            observations->requestedName = std::move(suggestedName);
            onResult(selectedPath);
            return true;
        };
    // Solo los bloqueos del destino definitivo deben llegar aquí; cancelar el diálogo no genera aviso.
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

    const bool shouldComplete = testCase.finalName != nullptr && !testCase.blockReason.has_value();
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
            DownloadTestTrigger::StartDownload, std::nullopt}),
    DownloadDestinationTest::CaseName);

}  // namespace sbp
