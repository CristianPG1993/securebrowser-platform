#pragma once

#include "rest/auth_session.h"
#include "rest_test_support.h"

#include <memory>

namespace sbp {

inline constexpr char ValidLoginResponse[] =
    R"({"accessToken":"fake-access","tokenType":"Bearer","expiresIn":900,"refreshToken":"fake-refresh"})";
inline constexpr char ValidRefreshResponse[] =
    R"({"accessToken":"new-access","tokenType":"Bearer","expiresIn":900,"refreshToken":"new-refresh"})";

// Cada prueba empieza con su propio servidor en modo Hold y una sesión autenticada.
class AuthRefreshTest : public RestClientTest {
protected:
    struct ProfileResult {
        int calls = 0;
        AuthResult result;
        std::optional<UserIdentity> identity;
    };

    // GoogleTest llama a SetUp antes de cada prueba para dejar una sesión autenticada en el servidor local.
    void SetUp() override;

    // Inicia sesión con el correo indicado y espera a que Login acepte los tokens de responseBody.
    void LoginAs(const QString& email, const QByteArray& responseBody);

    // Solicita el perfil y devuelve el objeto donde la callback guardará el resultado cuando llegue.
    [[nodiscard]] std::shared_ptr<ProfileResult> FetchProfile();

    // Espera la petición indicada por index y comprueba que su método HTTP y su ruta son los esperados.
    [[nodiscard]] bool WaitForRequest(std::size_t index, const QByteArray& methodAndPath);

    // Responde al GET del perfil con status y body si la cabecera Bearer contiene el accessToken esperado.
    [[nodiscard]] bool ReplyToProfile(std::size_t index, const QByteArray& accessToken, int status, const QByteArray& body);

    // Responde al POST de renovación si el JSON recibido contiene exactamente el refreshToken esperado.
    [[nodiscard]] bool ReplyToRefresh(std::size_t index, const QByteArray& response, const QString& refreshToken);

    RestTestServer server{{}, RestTestServer::Mode::Hold};
    RestClient client;
    std::unique_ptr<AuthSession> session;
};

}  // namespace sbp
