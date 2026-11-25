#pragma once

#include "ui/browser_host.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPointer>
#include <QPushButton>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>

class QEvent;
class QFileDialog;
class QMessageBox;

namespace sbp {

class BrowserWindow;

// Reúne la construcción y distribución de los controles de la ventana, sin depender de CEF.
// Se crea después de BrowserWindow y se destruye antes de ella, siempre en el hilo Qt.
class BrowserWindowUi final : public QObject {
public:
    // Los avisos del anfitrión se reciben ya preparados por quien coordina la sesión.
    BrowserWindowUi(BrowserWindow& window, BrowserHostCallbacks host_callbacks);
    BrowserWindowUi(const BrowserWindowUi&) = delete;
    BrowserWindowUi& operator=(const BrowserWindowUi&) = delete;

    // La sesión necesita el anfitrión para obtener su ventana nativa y coordinar el foco.
    BrowserHost& GetBrowserHost();

    // Recibe la URL actual en Qt; conserva el borrador mientras la sesión de edición siga abierta.
    void SetAddress(const QString& address);

    // Muestra el panel inferior y el nombre de la política publicada; solo se llama desde Qt.
    void SetPolicyName(const QString& policy_name);

    // Carga las opciones que proporciona la sesión, sin seleccionar ni publicar una política.
    void SetPolicyOptions(const QStringList& policy_names);

    // Refleja la selección confirmada por la sesión después de publicar una política válida.
    void SetSelectedPolicy(int index);

    // Guarda el receptor de solicitudes del desplegable; ambos se ejecutan en el hilo Qt.
    void SetPolicySelectionHandler(std::function<void(int)> on_policy_selected);

    // Conecta el botón una sola vez al arrancar en modo backend. Se habilita tras la primera consulta.
    void ConfigurePolicyRefresh(std::function<void()> on_refresh);
    void SetPolicyRefreshEnabled(bool enabled);

    // Muestra en Qt el texto de un bloqueo aplicado y ofrece la acción Inicio ya conectada.
    // La sesión prepara el motivo; la interfaz no evalúa políticas ni decide permisos.
    void ShowNavigationBlock(const QString& message);

    // Muestra en Qt un aviso de descarga bloqueada con el texto preparado por la sesión.
    // Solo presenta el motivo; no evalúa reglas ni ofrece reanudar la descarga cancelada.
    void ShowDownloadBlock(const QString& message);

    // Resultado no modal, independiente del panel de bloqueos; no decide permisos ni reintenta la descarga.
    void ShowDownloadResult(const QString& message);
    void ClearDownloadResult();

    // Retira el aviso cuando la sesión determina que ya no corresponde al estado mostrado.
    void ClearNavigationBlock();

    // Retira el foco lógico y el caret de la barra cuando el teclado ya pertenece a CEF.
    void ClearAddressFocus();

    // Pregunta desde el hilo Qt si se permite abandonar la página; devuelve true solo al elegir Salir.
    bool ConfirmPageLeave();

    // Cierra la pregunta si CEF la ha retirado antes de recibir la respuesta del usuario.
    void CancelPageLeave();

    // Abre Guardar como de forma asíncrona en Qt, con nombre editable y sin evaluar políticas.
    // Responde con la ruta elegida en UTF-8 o nullopt si el usuario cancela.
    void ShowDownloadSaveDialog(
        std::uint64_t operationId,
        const QString& suggestedName,
        std::function<void(std::optional<std::string>)> onResult);

    // Cierra el diálogo de una solicitud retirada; CEF descartará su respuesta tardía por operationId.
    void CancelDownloadSaveDialog(std::uint64_t operationId);

    // Cierra todos los diálogos pendientes antes de retirar los controles de la ventana.
    void CancelDownloadSaveDialogs();

    // Guarda la función que recibirá el texto al pulsar Enter; instalarla no la ejecuta.
    void SetAddressSubmitHandler(std::function<void(const QString&)> on_address_submitted);

    // Guarda la función que atenderá la petición de volver atrás; instalarla no navega.
    void SetBackRequestHandler(std::function<void()> on_back_requested);

    // CEF comunica su historial mediante una tarea Qt; el botón refleja si se puede retroceder.
    void SetBackAvailable(bool available);

    // Guarda la acción Adelante; la sesión la proporcionará al conectar este control con CEF.
    void SetForwardRequestHandler(std::function<void()> on_forward_requested);

    // Guarda la acción Recargar; la sesión la proporcionará al conectar este control con CEF.
    void SetReloadRequestHandler(std::function<void()> on_reload_requested);

    // Guarda una única acción Inicio y habilita sus botones de la barra y del aviso si existe receptor.
    void SetHomeRequestHandler(std::function<void()> on_home_requested);

    // Actualiza en Qt si se puede recargar la página actual.
    void SetReloadAvailable(bool available);

    // Actualiza en Qt si existe una entrada posterior en el historial.
    void SetForwardAvailable(bool available);

protected:
    // Qt entrega aquí los eventos del campo donde instalamos este objeto como filtro.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Prepara el panel inferior, oculto hasta que la sesión confirma el modo demo.
    void ConfigureStatusPanel();

    // Prepara un aviso no modal, separado del indicador exclusivo del modo demo.
    void ConfigureNavigationBlockPanel();
    // Conecta «Ir a inicio» con RequestHome, igual que el botón de la barra.
    void ConnectBlockHomeButton();

    // Prepara un mensaje de resultado que no sustituye los avisos de navegación o descarga bloqueadas.
    void ConfigureDownloadResultPanel();

    // Conecta la elección del usuario con una solicitud a la sesión.
    void ConnectPolicySelector();
    // Conserva la selección confirmada mientras la sesión valida y publica la nueva política.
    void RequestPolicySelection(int index);

    // Termina la edición al abandonar realmente la barra y muestra la última dirección de CEF.
    void FinishAddressEditing();

    // Conecta la activación normal del botón Qt con la acción proporcionada por la sesión.
    void ConnectBackButton();
    void RequestBack();
    void ConnectForwardButton();
    void RequestForward();
    void ConnectReloadButton();
    void RequestReload();
    void ConnectHomeButton();
    void RequestHome();

    // El orden permite destruir los controles antes del layout y de la ventana que los contiene.
    // Layout vertical para la barra de navegación, el contenido web y el panel inferior.
    QVBoxLayout window_layout_;

    // Fila horizontal que reúne las flechas de navegación y el campo de dirección.
    QHBoxLayout navigation_layout_;

    // Botón de navegación; CEF determina su disponibilidad a partir del historial.
    QPushButton back_button_;

    // Flecha Adelante; empieza deshabilitada hasta recibir la disponibilidad desde CEF.
    QPushButton forward_button_;

    // Botón Recargar; empieza deshabilitado hasta recibir la disponibilidad desde CEF.
    QPushButton reload_button_;

    // Inicio solicita el destino inicial; la política vigente sigue decidiendo si puede cargarse.
    QPushButton home_button_;

    // Campo de texto Qt donde el usuario escribe y edita la dirección web.
    QLineEdit address_input_;

    // Permite volver a consultar la política del dispositivo desde la barra.
    QPushButton policy_refresh_button_;

    // Última URL recibida del navegador; permite mostrarla al terminar de editar la barra.
    QString latest_address_;

    // La edición continúa durante Alt+Tab o un menú, aunque el campo pierda temporalmente el foco.
    bool address_editing_ = false;

    // Widget Qt con ventana nativa que aloja la ventana hija del navegador CEF.
    BrowserHost browser_host_;

    // Controles del panel inferior; se destruyen antes que su layout y el panel que los contiene.
    QWidget status_panel_;
    QVBoxLayout status_layout_;
    QLabel mode_label_;
    QLabel policy_label_;
    QComboBox policy_selector_;

    // Última selección confirmada; -1 significa que todavía no se ha confirmado ninguna.
    int selected_policy_index_ = -1;

    // El aviso tiene su propio panel: mostrar un bloqueo no debe activar los controles de demo.
    // El orden conserva la destrucción de los controles antes que su layout y su contenedor.
    QWidget block_panel_;
    QHBoxLayout block_layout_;
    QLabel block_message_;
    QPushButton block_home_button_;

    // Último resultado operativo; solo se modifica desde Qt y se limpia al solicitar un destino nuevo.
    QLabel download_result_message_;

    // Recibe un índice de presentación; la sesión decide qué snapshot construir y publicar.
    std::function<void(int)> on_policy_selected_;

    // Aviso síncrono de la interfaz a la sesión; ambos trabajan en el hilo principal Qt.
    std::function<void(const QString&)> on_address_submitted_;

    // Acción que la sesión proporcionará para solicitar el retroceso, sin exponer CEF a la interfaz.
    std::function<void()> on_back_requested_;

    // Acción para avanzar en el historial; se ejecuta únicamente al activar la flecha Adelante.
    std::function<void()> on_forward_requested_;

    // Acción para recargar la página; se ejecuta únicamente al activar el botón Recargar.
    std::function<void()> on_reload_requested_;

    // Se comparte como acción entre el botón Inicio y el aviso de bloqueo.
    std::function<void()> on_home_requested_;

    // Referencia no propietaria al diálogo local de ConfirmPageLeave; solo se utiliza en Qt.
    QMessageBox* page_leave_dialog_ = nullptr;

    // Solo Qt accede al mapa; la ventana posee los diálogos y QPointer detecta su destrucción.
    // Cada operación conserva su propio diálogo para permitir solicitudes de descarga independientes.
    std::map<std::uint64_t, QPointer<QFileDialog>> download_dialogs_;
};

}  // namespace sbp
