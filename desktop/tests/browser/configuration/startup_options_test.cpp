#include "app/startup_options.h"

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

namespace sbp {

struct StartupOptionsCase {
    const char* name;
    const char* commandLine;
    bool valid;
    bool demoEnabled = false;
    DemoPolicyPreset policy = DemoPolicyPreset::FreeNavigation;
};

class StartupOptionsTest : public ::testing::TestWithParam<StartupOptionsCase> {
public:
    static std::string CaseName(const ::testing::TestParamInfo<StartupOptionsCase>& info);
};

std::string StartupOptionsTest::CaseName(const ::testing::TestParamInfo<StartupOptionsCase>& info) {
    return info.param.name;
}

// Interpreta argumentos con el objeto real de CEF, sin arrancar otra ventana ni cargar políticas.
TEST_P(StartupOptionsTest, RequiresExplicitDemoModeAndKnownPolicy) {
    const auto& testCase = GetParam();
    const auto commandLine = CefCommandLine::CreateCommandLine();
    ASSERT_TRUE(commandLine);
    commandLine->InitFromString(testCase.commandLine);

    if (!testCase.valid) {
        EXPECT_THROW(static_cast<void>(StartupOptions::FromCommandLine(commandLine)), std::invalid_argument);
        return;
    }

    const auto options = StartupOptions::FromCommandLine(commandLine);
    EXPECT_EQ(testCase.demoEnabled, options.IsUrlFilteringDemo());
    if (testCase.demoEnabled) {
        EXPECT_EQ(testCase.policy, options.InitialDemoPolicy());
    }
}

// Una dependencia inexistente no se interpreta como una ejecución normal ni como demo.
TEST(StartupOptionsValidationTest, RejectsMissingCommandLine) {
    EXPECT_THROW(static_cast<void>(StartupOptions::FromCommandLine(nullptr)), std::invalid_argument);
}

INSTANTIATE_TEST_SUITE_P(CommandLine, StartupOptionsTest, ::testing::Values(
    // Arrancar sin opciones mantiene el modo normal y no activa ninguna política demo.
    StartupOptionsCase{"NormalStartup", "SecureBrowser.exe", true},
    // Las opciones propias de Chromium no activan la demostración.
    StartupOptionsCase{"UnrelatedOption", "SecureBrowser.exe --lang=es", true},
    // Mencionar el texto dentro de otra opción no equivale a activar la opción demo.
    StartupOptionsCase{"DemoTextIsNotDemoSwitch", "SecureBrowser.exe --other=demo-url-filtering", true},
    // Activar la demo sin elegir política selecciona la libre como valor inicial.
    StartupOptionsCase{"DemoDefaultsToFree", "SecureBrowser.exe --demo-url-filtering", true, true},
    // La selección explícita de free conserva la política inicial libre.
    StartupOptionsCase{"ExplicitFreePolicy", "SecureBrowser.exe --demo-url-filtering --demo-policy=free", true, true},
    // La selección explícita de restricted se traduce al preset restrictivo.
    StartupOptionsCase{"ExplicitRestrictedPolicy", "SecureBrowser.exe --demo-url-filtering --demo-policy=restricted",
        true, true, DemoPolicyPreset::RestrictedNavigation},
    // Pedir la política libre sin activar la demo es un error, no una activación implícita.
    StartupOptionsCase{"FreePolicyRequiresDemo", "SecureBrowser.exe --demo-policy=free", false},
    // Pedir la política restrictiva tampoco permite omitir la activación explícita.
    StartupOptionsCase{"RestrictedPolicyRequiresDemo", "SecureBrowser.exe --demo-policy=restricted", false},
    // Una política desconocida no se sustituye silenciosamente por la libre.
    StartupOptionsCase{"UnknownPolicyRejected", "SecureBrowser.exe --demo-url-filtering --demo-policy=unknown", false},
    // Una selección vacía no equivale a omitir la opción de política.
    StartupOptionsCase{"EmptyPolicyRejected", "SecureBrowser.exe --demo-url-filtering --demo-policy=", false},
    // El interruptor demo no admite un valor booleano: solo su presencia lo activa.
    StartupOptionsCase{"DemoSwitchValueRejected", "SecureBrowser.exe --demo-url-filtering=false", false}),
    StartupOptionsTest::CaseName);

}  // namespace sbp
