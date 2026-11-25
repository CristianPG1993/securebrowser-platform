#include <QCoreApplication>
#include <QNetworkProxy>

#include <gtest/gtest.h>

// Ejecutable de consola: procesa eventos de red sin ventanas, CEF ni credenciales reales.
int main(int argc, char* argv[]) {
    // GoogleTest interpreta opciones como --gtest_filter y --gtest_list_tests antes de ejecutar los casos.
    ::testing::InitGoogleTest(&argc, argv);
    // QCoreApplication prepara Qt sin widgets. Los auxiliares de cada caso procesarán los eventos de red.
    QCoreApplication app(argc, argv);
    // Cliente y servidor se comunican por loopback, con independencia de los proxies configurados en el equipo.
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    // Ejecuta los TEST_F registrados y devuelve un código distinto de cero si alguno falla.
    return RUN_ALL_TESTS();
}
