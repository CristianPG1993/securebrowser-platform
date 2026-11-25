#include "ui/login_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>

namespace sbp {

LoginDialog::LoginDialog(QWidget* parent, const QString& error_message)
    : QDialog(parent), email_input_(this), password_input_(this) {
    setWindowTitle(QStringLiteral("Iniciar sesión"));
    password_input_.setEchoMode(QLineEdit::Password);

    auto* form = new QFormLayout(this);
    if (!error_message.isEmpty()) {
        form->addRow(new QLabel(error_message, this));
    }
    form->addRow(QStringLiteral("&Correo"), &email_input_);
    form->addRow(QStringLiteral("C&ontraseña"), &password_input_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Iniciar sesión"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Cancelar"));
    form->addRow(buttons);

    // Qt resuelve estas señales por nombre para conservar la carga diferida de QtWidgets en Windows.
    connect(buttons, SIGNAL(accepted()), this, SLOT(accept()));
    connect(buttons, SIGNAL(rejected()), this, SLOT(reject()));
}

QString LoginDialog::Email() const {
    return email_input_.text().trimmed();
}

QString LoginDialog::Password() const {
    return password_input_.text();
}

void LoginDialog::accept() {
    if (Email().isEmpty()) {
        email_input_.setFocus();
        return;
    }
    if (password_input_.text().isEmpty()) {
        password_input_.setFocus();
        return;
    }
    QDialog::accept();
}

}  // namespace sbp
