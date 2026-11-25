#include "ui/browser_window_ui.h"

#include "ui/browser_window.h"

#include <QAbstractButton>
#include <QByteArray>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMessageBox>
#include <QStyle>

#include <cstdio>

#include <windows.h>

#include <utility>

namespace sbp {

BrowserWindowUi::BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks)
    : QObject(&window),
      window_layout_(&window),
      back_button_(&window),
      forward_button_(&window),
      reload_button_(&window),
      home_button_(&window),
      address_input_(&window),
      policy_refresh_button_(QStringLiteral("Actualizar política"), &window),
      browser_host_(std::move(host_callbacks), &window),
      status_panel_(&window),
      status_layout_(&status_panel_),
      mode_label_(&status_panel_),
      policy_label_(&status_panel_),
      policy_selector_(&status_panel_),
      block_panel_(&window),
      block_layout_(&block_panel_),
      block_message_(&block_panel_),
      block_home_button_(&block_panel_),
      download_result_message_(&window) {
    // Aquí se configura el aspecto inicial de la ventana y la distribución de sus controles.
    window.setWindowTitle("SecureBrowser");
    window.resize(1024, 768);
    window_layout_.setContentsMargins(0, 0, 0, 0);

    // CEF habilitará el botón cuando exista una página anterior en su historial.
    back_button_.setObjectName("back_button");
    // El icono estándar de Qt adapta la flecha al estilo y al estado habilitado del botón.
    back_button_.setIcon(window.style()->standardIcon(QStyle::SP_ArrowBack));
    back_button_.setToolTip("Atrás");
    back_button_.setAccessibleName("Volver a la página anterior");
    back_button_.setEnabled(false);
    back_button_.installEventFilter(this);
    ConnectBackButton();

    // CEF habilitará Adelante cuando exista una página posterior en el historial.
    forward_button_.setObjectName("forward_button");
    forward_button_.setIcon(window.style()->standardIcon(QStyle::SP_ArrowForward));
    forward_button_.setToolTip("Adelante");
    forward_button_.setAccessibleName("Ir a la página siguiente");
    forward_button_.setEnabled(false);
    forward_button_.installEventFilter(this);
    ConnectForwardButton();

    // CEF habilitará Recargar cuando exista una entrada en el historial, aunque siga cargando.
    reload_button_.setObjectName("reload_button");
    reload_button_.setIcon(window.style()->standardIcon(QStyle::SP_BrowserReload));
    reload_button_.setToolTip("Recargar");
    reload_button_.setAccessibleName("Recargar la página actual");
    reload_button_.setEnabled(false);
    reload_button_.installEventFilter(this);
    ConnectReloadButton();

    // Inicio conserva el mismo recorrido Qt→CEF que el resto de controles de navegación.
    home_button_.setObjectName("home_button");
    home_button_.setIcon(window.style()->standardIcon(QStyle::SP_DirHomeIcon));
    home_button_.setToolTip(QStringLiteral("Inicio"));
    home_button_.setAccessibleName(QStringLiteral("Ir a la página de inicio"));
    home_button_.setEnabled(false);
    home_button_.installEventFilter(this);
    ConnectHomeButton();

    // El campo permite editar una dirección; Enter avisará a la sesión mediante su callback.
    address_input_.setObjectName("address_input");
    address_input_.setAccessibleName("Dirección web");
    address_input_.setPlaceholderText("Introduce una URL HTTP o HTTPS");
    // Qt consultará eventFilter antes de entregar los eventos de este campo.
    // No requiere Q_OBJECT ni importar datos de QLineEdit para conectar una señal tipada.
    address_input_.installEventFilter(this);

    // Las flechas quedan a la izquierda; el factor 1 reserva el espacio restante a la dirección.
    navigation_layout_.addWidget(&back_button_);
    navigation_layout_.addWidget(&forward_button_);
    navigation_layout_.addWidget(&reload_button_);
    navigation_layout_.addWidget(&home_button_);
    navigation_layout_.addWidget(&address_input_, 1);
    policy_refresh_button_.hide();
    navigation_layout_.addWidget(&policy_refresh_button_);
    ConfigureStatusPanel();
    ConfigureNavigationBlockPanel();
    ConfigureDownloadResultPanel();
    // El navegador ocupa el espacio restante y el panel queda debajo, sin superponer contenido.
    window_layout_.addLayout(&navigation_layout_);
    window_layout_.addWidget(&browser_host_, 1);
    window_layout_.addWidget(&block_panel_);
    window_layout_.addWidget(&download_result_message_);
    window_layout_.addWidget(&status_panel_);
}

void BrowserWindowUi::ConfigurePolicyRefresh(std::function<void()> on_refresh) {
    const auto* meta = policy_refresh_button_.metaObject();
    const auto clicked = meta->method(meta->indexOfSignal("clicked(bool)"));
    if (!QMetaObject::connect(&policy_refresh_button_, clicked, this, std::move(on_refresh))) {
        std::fputs("Qt policy refresh button could not be connected.\n", stderr);
        return;
    }
    policy_refresh_button_.installEventFilter(this);
    policy_refresh_button_.setEnabled(false);
    policy_refresh_button_.show();
}

void BrowserWindowUi::SetPolicyRefreshEnabled(bool enabled) {
    policy_refresh_button_.setEnabled(enabled);
}

void BrowserWindowUi::ConfigureStatusPanel() {
    status_panel_.setObjectName("status_panel");
    status_panel_.setAccessibleName(QStringLiteral("Estado de navegación"));
    status_layout_.setContentsMargins(8, 4, 8, 4);
    status_layout_.setSpacing(2);

    mode_label_.setObjectName("mode_label");
    mode_label_.setTextFormat(Qt::PlainText);
    mode_label_.setText(QStringLiteral("Modo demostración — política local sin caducidad"));
    mode_label_.setWordWrap(true);

    policy_label_.setObjectName("policy_label");
    // El nombre de política se presenta como texto, sin interpretar etiquetas HTML.
    policy_label_.setTextFormat(Qt::PlainText);
    policy_label_.setWordWrap(true);

    // La sesión cargará las opciones y confirmará la política realmente publicada.
    policy_selector_.setObjectName("policy_selector");
    policy_selector_.setAccessibleName(QStringLiteral("Seleccionar política"));
    policy_selector_.setToolTip(QStringLiteral(
        "Cambiar la política se aplica a nuevas navegaciones. Las políticas demo no caducan."));
    policy_selector_.setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    policy_selector_.setMinimumContentsLength(24);
    policy_selector_.setEnabled(false);
    policy_selector_.installEventFilter(this);
    ConnectPolicySelector();

    status_layout_.addWidget(&mode_label_);
    status_layout_.addWidget(&policy_label_);
    status_layout_.addWidget(&policy_selector_);
    status_panel_.hide();
}

void BrowserWindowUi::ConfigureNavigationBlockPanel() {
    block_panel_.setObjectName("navigation_block_panel");
    block_panel_.setAccessibleName(QStringLiteral("Aviso de navegación bloqueada"));
    block_layout_.setContentsMargins(8, 6, 8, 6);
    block_layout_.setSpacing(8);

    block_message_.setObjectName("navigation_block_message");
    // El texto puede incluir un host; nunca se interpreta como HTML ni como un enlace.
    block_message_.setTextFormat(Qt::PlainText);
    block_message_.setWordWrap(true);

    block_home_button_.setObjectName("navigation_block_home_button");
    block_home_button_.setText(QStringLiteral("Ir a inicio"));
    block_home_button_.setAccessibleName(QStringLiteral("Ir a inicio desde el aviso de bloqueo"));
    block_home_button_.setEnabled(false);
    block_home_button_.installEventFilter(this);
    ConnectBlockHomeButton();

    block_layout_.addWidget(&block_message_, 1);
    block_layout_.addWidget(&block_home_button_);
    block_panel_.hide();
}

void BrowserWindowUi::ConfigureDownloadResultPanel() {
    download_result_message_.setObjectName("download_result_message");
    download_result_message_.setAccessibleName(QStringLiteral("Resultado de la descarga"));
    download_result_message_.setTextFormat(Qt::PlainText);
    download_result_message_.setWordWrap(true);
    download_result_message_.setMargin(8);
    download_result_message_.hide();
}

void BrowserWindowUi::ShowDownloadResult(const QString& message) {
    download_result_message_.setText(message);
    download_result_message_.show();
}

void BrowserWindowUi::ClearDownloadResult() {
    download_result_message_.clear();
    download_result_message_.hide();
}

void BrowserWindowUi::ShowNavigationBlock(const QString& message) {
    // La sesión solo llama desde Qt, después de recibir el aviso de bloqueo aplicado por CEF.
    block_message_.setText(message);
    block_panel_.show();
}

void BrowserWindowUi::ClearNavigationBlock() {
    // Retirar el aviso no cambia la política ni inicia una navegación.
    block_message_.clear();
    block_panel_.hide();
}

void BrowserWindowUi::ShowDownloadBlock(const QString& message) {
    // La sesión solo llama desde Qt, después de recibir el aviso de bloqueo de descarga aplicado por CEF.
    ClearDownloadResult();
    block_message_.setText(message);
    block_panel_.show();
}

void BrowserWindowUi::SetPolicyName(const QString& policy_name) {
    policy_label_.setText(QStringLiteral("Política: %1").arg(policy_name));
    status_panel_.show();
}

void BrowserWindowUi::SetPolicyOptions(const QStringList& policy_names) {
    // Cargar textos no selecciona ni publica nada; la sesión confirmará después un índice.
    selected_policy_index_ = -1;
    policy_selector_.setEnabled(false);
    policy_selector_.clear();
    policy_selector_.addItems(policy_names);
    policy_selector_.setCurrentIndex(-1);
}

void BrowserWindowUi::SetSelectedPolicy(int index) {
    if (index < 0 || index >= policy_selector_.count()) {
        return;
    }
    selected_policy_index_ = index;
    // activated solo procede de una elección del usuario; esta actualización no solicita otra publicación.
    policy_selector_.setCurrentIndex(index);
    policy_selector_.setEnabled(static_cast<bool>(on_policy_selected_));
}

void BrowserWindowUi::SetPolicySelectionHandler(std::function<void(int)> on_policy_selected) {
    // Instala el receptor sin ejecutarlo; su captura pertenece a la vida de estos controles Qt.
    on_policy_selected_ = std::move(on_policy_selected);
    policy_selector_.setEnabled(on_policy_selected_ && selected_policy_index_ >= 0);
}

void BrowserWindowUi::ConnectPolicySelector() {
    // Los metadatos evitan importar staticMetaObject y conservan la carga diferida de Qt del bootstrap.
    const QMetaObject* selector_meta = policy_selector_.metaObject();
    const int activated_index = selector_meta->indexOfSignal("activated(int)");
    // Qt entrega el índice en su hilo principal; this vive mientras existe la conexión.
    const auto connection = QMetaObject::connect(
        &policy_selector_, selector_meta->method(activated_index), this,
        [this](int index) { RequestPolicySelection(index); });
    if (!connection) {
        std::fputs("Qt policy selector activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::RequestPolicySelection(int index) {
    // Conserva la confirmación anterior si la sesión rechaza o no puede publicar la solicitud.
    policy_selector_.setCurrentIndex(selected_policy_index_);
    if (index < 0 || index >= policy_selector_.count()
        || index == selected_policy_index_ || !on_policy_selected_) {
        return;
    }
    on_policy_selected_(index);
}

BrowserHost& BrowserWindowUi::GetBrowserHost() {
    // Devuelve el control existente por referencia; no construye ni copia otro anfitrión.
    return browser_host_;
}

void BrowserWindowUi::SetAddress(const QString& address) {
    // Recordamos siempre la URL real, aunque el usuario esté escribiendo otra dirección.
    latest_address_ = address;
    // La sesión de edición conserva el borrador también durante Alt+Tab o un menú contextual.
    if (address_editing_) {
        return;
    }
    // QLineEdit guarda el texto y Qt programa su repintado para mostrar la nueva dirección.
    address_input_.setText(address);
}

void BrowserWindowUi::ClearAddressFocus() {
    // clearFocus entrega a QLineEdit su FocusOut normal, que detiene el caret.
    if (address_input_.hasFocus()) {
        address_input_.clearFocus();
    }
    // El puente llama aquí cuando el teclado pertenece a CEF, incluso tras una pérdida temporal de foco.
    FinishAddressEditing();
}

bool BrowserWindowUi::ConfirmPageLeave() {
    // No abrimos otro diálogo dentro del bucle modal de una pregunta que todavía está pendiente.
    if (page_leave_dialog_ != nullptr) {
        return false;
    }
    // El diálogo pertenece a nuestra ventana y toda su interacción se procesa en el hilo Qt.
    QMessageBox dialog(QMessageBox::Warning, QStringLiteral("Abandonar página"),
                       QStringLiteral("Puede haber cambios sin guardar. ¿Quieres salir de esta página?"),
                       QMessageBox::Discard | QMessageBox::Cancel, address_input_.window());
    dialog.button(QMessageBox::Discard)->setText(QStringLiteral("Salir"));
    dialog.button(QMessageBox::Cancel)->setText(QStringLiteral("Quedarme"));
    // Quedarme es la opción predeterminada; Escape y la X también cancelan la salida.
    dialog.setDefaultButton(QMessageBox::Cancel);
    dialog.setEscapeButton(QMessageBox::Cancel);
    dialog.setWindowModality(Qt::WindowModal);
    // Guardamos un acceso temporal para poder rechazarlo si CEF retira la petición durante exec.
    page_leave_dialog_ = &dialog;
    // Solo espera el hilo Qt en su bucle modal; el hilo UI de CEF sigue atendiendo sus tareas.
    const bool allow_leave = dialog.exec() == QMessageBox::Discard;
    page_leave_dialog_ = nullptr;
    return allow_leave;
}

void BrowserWindowUi::CancelPageLeave() {
    // reject termina el diálogo como Quedarme; no destruye el objeto local de ConfirmPageLeave.
    if (page_leave_dialog_ != nullptr) {
        page_leave_dialog_->reject();
    }
}

// Presenta el destino editable en Qt; el receptor comprobará después si permite continuar la descarga.
void BrowserWindowUi::ShowDownloadSaveDialog(
    std::uint64_t operationId,
    const QString& suggestedName,
    std::function<void(std::optional<std::string>)> onResult) {
    if (!onResult) {
        return;
    }
    if (download_dialogs_.find(operationId) != download_dialogs_.end()) {
        // Una petición duplicada es un fallo de entrega, no una cancelación voluntaria del usuario.
        onResult(std::string{});
        return;
    }

    // Una solicitud nueva retira el resultado anterior; Cancelar no se presenta como un fallo.
    ClearDownloadResult();
    // La ventana posee el diálogo; open permite responder por señal sin ejecutar un bucle modal anidado.
    auto* dialog = new QFileDialog(address_input_.window(), QStringLiteral("Guardar descarga"));
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setNameFilter(QStringLiteral("Todos los archivos (*)"));
    dialog->selectFile(suggestedName);
    download_dialogs_.emplace(operationId, dialog);

    // Consulta la señal en tiempo de ejecución para evitar importar QDialog::staticMetaObject como dato.
    // Así se conserva la carga diferida de Qt que necesita el arranque de los subprocesos CEF.
    const QMetaObject* dialog_meta = dialog->metaObject();
    const int finished_index = dialog_meta->indexOfSignal("finished(int)");
    // Conserva una respuesta alternativa por si Qt rechaza la conexión después de mover onResult a la lambda.
    const auto onConnectionFailure = onResult;
    // finished se procesa en Qt; el contexto this desconecta la función si se destruyen los controles.
    const auto connection = QMetaObject::connect(dialog, dialog_meta->method(finished_index), this,
        [this, operationId, dialog, onResult = std::move(onResult)](int result) {
            // Retira el registro antes de responder para consumir esta petición una sola vez.
            const auto pending = download_dialogs_.find(operationId);
            if (pending == download_dialogs_.end() || pending->second.data() != dialog) {
                return;
            }
            // Elimina el diálogo de la lista de pendientes antes de procesar la respuesta.
            download_dialogs_.erase(pending);

            std::optional<std::string> path;
            if (result == QDialog::Accepted) {
                // Una confirmación sin ruta devuelve una cadena vacía: CEF la rechazará como destino inválido.
                path = std::string{};
                const QStringList files = dialog->selectedFiles();
                if (files.size() == 1) {
                    // Qt devuelve separadores /; la operación de guardado de CEF usa el Shell de Windows.
                    // Convierte solo los separadores al formato nativo, conservando carpeta, nombre y extensión.
                    const QString nativePath = QDir::toNativeSeparators(files.front());
                    // QString guarda texto en UTF-16; toUtf8 lo convierte a los bytes UTF-8 que espera CEF.
                    const QByteArray utf8Path = nativePath.toUtf8();
                    // constData apunta al inicio de los bytes y size indica cuántos hay, no cuántos caracteres.
                    // static_cast convierte esa cantidad al tipo de tamaño std::size_t que espera el constructor.
                    // std::string copia los bytes tal cual, sin otra conversión; path conserva su propia cadena.
                    path = std::string(utf8Path.constData(), static_cast<std::size_t>(utf8Path.size()));
                }
            }

            // La destrucción queda aplazada hasta salir de la señal; la respuesta solo transporta datos propios.
            dialog->deleteLater();
            onResult(std::move(path));
        });

    if (!connection) {
        // No abre un diálogo sin respuesta: retira su registro y comunica un destino inválido a CEF.
        download_dialogs_.erase(operationId);
        dialog->deleteLater();
        std::fputs("Qt download save dialog completion could not be connected.\n", stderr);
        onConnectionFailure(std::string{});
        return;
    }

    dialog->open();
}

// Cierra en Qt el diálogo de una solicitud retirada, sin afectar a otras operaciones.
void BrowserWindowUi::CancelDownloadSaveDialog(std::uint64_t operationId) {
    const auto pending = download_dialogs_.find(operationId);
    if (pending == download_dialogs_.end()) {
        return;
    }

    // Copia el puntero vigilado antes de reject, cuya señal finished puede retirar la entrada del mapa.
    const QPointer<QFileDialog> dialog = pending->second;
    if (!dialog) {
        // El diálogo ya se destruyó; solo queda retirar su registro.
        download_dialogs_.erase(pending);
        return;
    }

    // finished retirará el registro, responderá con nullopt y programará la destrucción del diálogo.
    // No se utiliza pending después de esta llamada porque su entrada puede haber sido eliminada.
    dialog->reject();
}

// Solicita cerrar todos los diálogos pendientes desde el hilo Qt.
void BrowserWindowUi::CancelDownloadSaveDialogs() {
    // Copia las entradas y sus QPointer, sin duplicar los diálogos a los que apuntan.
    // finished puede borrar entradas del mapa original al rechazar cada diálogo.
    const auto dialogs = download_dialogs_;
    for (const auto& entry : dialogs) {
        CancelDownloadSaveDialog(entry.first);
    }
}

void BrowserWindowUi::FinishAddressEditing() {
    if (address_editing_) {
        address_editing_ = false;
        address_input_.setText(latest_address_);
    }
}

void BrowserWindowUi::SetAddressSubmitHandler(
    std::function<void(const QString&)> on_address_submitted) {
    on_address_submitted_ = std::move(on_address_submitted);
}

void BrowserWindowUi::SetBackRequestHandler(std::function<void()> on_back_requested) {
    // Guardamos la acción; clicked la ejecutará a través de RequestBack cuando se active el botón.
    on_back_requested_ = std::move(on_back_requested);
}

void BrowserWindowUi::SetBackAvailable(bool available) {
    back_button_.setEnabled(available);
}

void BrowserWindowUi::SetForwardRequestHandler(std::function<void()> on_forward_requested) {
    // Conservamos la función para ejecutarla más tarde, cuando Qt entregue la activación del botón.
    on_forward_requested_ = std::move(on_forward_requested);
}

void BrowserWindowUi::SetReloadRequestHandler(std::function<void()> on_reload_requested) {
    on_reload_requested_ = std::move(on_reload_requested);
}

void BrowserWindowUi::SetHomeRequestHandler(std::function<void()> on_home_requested) {
    // Ambos botones ejecutan el mismo receptor; no se conserva una segunda función para el aviso.
    on_home_requested_ = std::move(on_home_requested);
    home_button_.setEnabled(static_cast<bool>(on_home_requested_));
    block_home_button_.setEnabled(static_cast<bool>(on_home_requested_));
}

void BrowserWindowUi::SetForwardAvailable(bool available) {
    forward_button_.setEnabled(available);
}

void BrowserWindowUi::ConnectBackButton() {
    // Consultamos la señal en tiempo de ejecución para conservar la carga diferida de Qt en el bootstrap.
    // QMetaObject::connect entrega la señal a una lambda, sin añadir Q_OBJECT a nuestra clase.
    const QMetaObject* button_meta = back_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // La lambda llama al método privado sin importar el staticMetaObject de nuestra clase base.
    const auto connection = QMetaObject::connect(&back_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestBack(); });
    if (!connection) {
        std::fputs("Qt back button activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::RequestBack() {
    // Qt ya ha reconocido la activación con ratón o teclado; ejecutamos la acción guardada.
    if (on_back_requested_) {
        on_back_requested_();
    }
}

void BrowserWindowUi::ConnectForwardButton() {
    // La conexión por metadatos conserva la carga diferida de Qt, igual que en el botón Atrás.
    const QMetaObject* button_meta = forward_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // Esta lambda recibe la señal Qt y delega la acción en nuestro método privado.
    const auto connection = QMetaObject::connect(&forward_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestForward(); });
    if (!connection) {
        std::fputs("Qt forward button activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::ConnectReloadButton() {
    const QMetaObject* button_meta = reload_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // La señal Qt delega la activación del botón en nuestro método privado.
    const auto connection = QMetaObject::connect(&reload_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestReload(); });
    if (!connection) {
        std::fputs("Qt reload button activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::RequestReload() {
    if (on_reload_requested_) {
        on_reload_requested_();
    }
}

void BrowserWindowUi::ConnectHomeButton() {
    // La conexión por metadatos mantiene la carga diferida de Qt, igual que las otras flechas.
    const QMetaObject* button_meta = home_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // Qt entrega la activación en su hilo; this pertenece a la vida de la interfaz.
    const auto connection = QMetaObject::connect(&home_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestHome(); });
    if (!connection) {
        std::fputs("Qt home button activation could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::ConnectBlockHomeButton() {
    // La conexión por metadatos conserva la carga diferida de Qt del bootstrap.
    const QMetaObject* button_meta = block_home_button_.metaObject();
    const int clicked_index = button_meta->indexOfSignal("clicked(bool)");
    // Este botón reutiliza RequestHome en el hilo Qt y no introduce una segunda ruta de navegación.
    const auto connection = QMetaObject::connect(&block_home_button_, button_meta->method(clicked_index),
                                                this, [this] { RequestHome(); });
    if (!connection) {
        std::fputs("Qt blocked navigation home action could not be connected.\n", stderr);
    }
}

void BrowserWindowUi::RequestHome() {
    if (on_home_requested_) {
        on_home_requested_();
    }
}

void BrowserWindowUi::SetReloadAvailable(bool available) {
    reload_button_.setEnabled(available);
}

void BrowserWindowUi::RequestForward() {
    // Qt reconoce la activación; ejecutamos la acción que instaló el coordinador de navegación.
    if (on_forward_requested_) {
        on_forward_requested_();
    }
}

bool BrowserWindowUi::eventFilter(QObject* watched, QEvent* event) {
    // Entrar en la barra inicia una edición que puede sobrevivir a pérdidas temporales de foco.
    if (watched == &address_input_ && event->type() == QEvent::FocusIn) {
        address_editing_ = true;
    }
    // Alt+Tab y los menús no terminan la edición ni sustituyen el borrador por la URL de CEF.
    if (watched == &address_input_ && event->type() == QEvent::FocusOut) {
        const auto* focus_event = static_cast<const QFocusEvent*>(event);
        if (focus_event->reason() != Qt::ActiveWindowFocusReason
            && focus_event->reason() != Qt::PopupFocusReason) {
            FinishAddressEditing();
        }
    }
    // Pasar a otro control Qt termina la edición, también al regresar desde otra aplicación.
    if ((watched == &back_button_ || watched == &forward_button_ || watched == &reload_button_
         || watched == &home_button_ || watched == &policy_refresh_button_
         || watched == &policy_selector_ || watched == &block_home_button_)
        && event->type() == QEvent::FocusIn) {
        FinishAddressEditing();
    }
    // CEF puede conservar el foco nativo aunque Qt recuerde un control de la barra como enfocado.
    // Al entrar con ratón o teclado, devolvemos los mensajes de Windows a la ventana Qt.
    if ((watched == &address_input_ || watched == &back_button_ || watched == &forward_button_
         || watched == &reload_button_ || watched == &home_button_ || watched == &policy_selector_
         || watched == &policy_refresh_button_ || watched == &block_home_button_)
        && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::FocusIn)) {
        QWidget* qt_window = address_input_.window();
        if (qt_window->isActiveWindow()) {
            const HWND qt_window_handle = reinterpret_cast<HWND>(qt_window->winId());
            // Solo cambiamos el foco real si sigue en otra ventana; Qt conserva su control de destino.
            if (GetFocus() != qt_window_handle) {
                ::SetFocus(qt_window_handle);
            }
        }
    }
    // Solo observamos pulsaciones del campo de dirección; el resto mantiene su comportamiento Qt.
    if (watched == &address_input_ && event->type() == QEvent::KeyPress) {
        const auto* key_event = static_cast<QKeyEvent*>(event);
        // Qt distingue Enter del teclado principal y Enter del teclado numérico.
        if (key_event->key() == Qt::Key_Return || key_event->key() == Qt::Key_Enter) {
            // Mantener Enter pulsado no debe producir solicitudes repetidas automáticamente.
            if (!key_event->isAutoRepeat() && on_address_submitted_) {
                // Conservamos una copia del texto durante la llamada; no se transforma ni valida aún.
                const QString address = address_input_.text();
                on_address_submitted_(address);
            }
            // La pulsación ya está atendida; no la propagamos a otros controles de la ventana.
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

}  // namespace sbp
