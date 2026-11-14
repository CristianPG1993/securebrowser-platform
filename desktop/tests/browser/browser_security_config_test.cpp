#include "browser/browser_security_config.h"

#include <gtest/gtest.h>

namespace sbp {

// La protección no debe borrar otras opciones de arranque al imponer su estado de seguridad.
TEST(BrowserSecurityConfigTest, PreservesOtherStartupOptionsAndDisablesBfcache) {
    const auto commandLine = CefCommandLine::CreateCommandLine();
    commandLine->InitFromString("sbp-security-config-test");
    commandLine->AppendSwitchWithValue("disable-features", "OtherFeature,BackForwardCache<Trial,AnotherFeature");
    commandLine->AppendSwitchWithValue("enable-features", "BackForwardCache,EnabledFeature");
    commandLine->AppendSwitchWithValue("lang", "es");

    BrowserSecurityConfig::Apply(commandLine);

    EXPECT_EQ("BackForwardCache,OtherFeature,AnotherFeature",
              commandLine->GetSwitchValue("disable-features").ToString());
    EXPECT_EQ("BackForwardCache,EnabledFeature",
              commandLine->GetSwitchValue("enable-features").ToString());
    EXPECT_EQ("es", commandLine->GetSwitchValue("lang").ToString());
}

// Reaplicar la configuración conserva el resultado y sustituye entradas que usarían el valor por defecto.
TEST(BrowserSecurityConfigTest, OverridesDefaultEntryAndCanBeAppliedAgain) {
    const auto commandLine = CefCommandLine::CreateCommandLine();
    commandLine->InitFromString("sbp-security-config-test");
    commandLine->AppendSwitchWithValue("disable-features", "*BackForwardCache<Trial,OtherFeature");

    BrowserSecurityConfig::Apply(commandLine);
    EXPECT_EQ("BackForwardCache,OtherFeature",
              commandLine->GetSwitchValue("disable-features").ToString());

    BrowserSecurityConfig::Apply(commandLine);
    EXPECT_EQ("BackForwardCache,OtherFeature",
              commandLine->GetSwitchValue("disable-features").ToString());
}

}  // namespace sbp
