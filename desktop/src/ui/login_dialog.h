#pragma once

#include <QDialog>
#include <QLineEdit>

namespace sbp {

// Formulario de acceso al backend. La sesión lee los campos cuando el usuario confirma.
class LoginDialog final : public QDialog {
public:
    explicit LoginDialog(QWidget* parent, const QString& error_message = {});

    // Recorta los espacios exteriores del correo y conserva la contraseña tal como se escribió.
    [[nodiscard]] QString Email() const;
    [[nodiscard]] QString Password() const;

    // Mantiene el formulario abierto mientras falte alguno de los dos campos.
    void accept() override;

private:
    QLineEdit email_input_;
    QLineEdit password_input_;
};

}  // namespace sbp
