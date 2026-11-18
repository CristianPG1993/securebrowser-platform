#include "download_test_client.h"

#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_values.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace sbp {

// Conserva los datos del caso y comparte el handler; crear el cliente no crea todavía el navegador CEF.
DownloadTestClient::DownloadTestClient(CefRefPtr<SecurityDownloadHandler> handler,
                                       std::string pageUrl, std::string downloadUrl,
                                       DownloadTestTrigger trigger)
    : handler_(std::move(handler)),
      pageUrl_(std::move(pageUrl)),
      downloadUrl_(std::move(downloadUrl)),
      trigger_(trigger) {
    if (!handler_) {
        throw std::invalid_argument("DownloadTestClient requires a download handler.");
    }
}

// GoogleTest espera el resultado mientras CEF procesa la creación, la descarga y el cierre en su hilo UI.
DownloadTestResult DownloadTestClient::Run() {
    if (CefCurrentlyOn(TID_UI)) {
        throw std::logic_error("DownloadTestClient::Run must not block the CEF UI thread.");
    }

    auto completion = completion_.get_future();
    // BindOnce conserva el cliente hasta ejecutar la tarea; ningún navegador se crea desde GoogleTest.
    if (!CefPostTask(TID_UI, base::BindOnce(&DownloadTestClient::CreateBrowser,
                                          CefRefPtr<DownloadTestClient>(this)))) {
        DownloadTestResult failure;
        failure.error = "Could not post download test browser creation to the CEF UI thread.";
        return failure;
    }

    if (completion.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
        // No permite continuar hacia CefShutdown con un navegador cuyo cierre no se haya confirmado.
        std::fputs("CEF download test timed out before browser closure.\n", stderr);
        std::abort();
    }
    return completion.get();
}

// Este cliente observa cuándo se crea y se cierra el navegador de la prueba.
CefRefPtr<CefLifeSpanHandler> DownloadTestClient::GetLifeSpanHandler() {
    return this;
}

// CEF entrega aquí los callbacks reales; el cliente los registrará y delegará en handler_.
CefRefPtr<CefDownloadHandler> DownloadTestClient::GetDownloadHandler() {
    return this;
}

// Este mismo cliente espera la carga del documento de origen antes de solicitar la descarga.
CefRefPtr<CefLoadHandler> DownloadTestClient::GetLoadHandler() {
    return this;
}

// Crea el navegador real de prueba; solo se invoca desde la tarea publicada por Run en UI CEF.
void DownloadTestClient::CreateBrowser() {
    CEF_REQUIRE_UI_THREAD();
    CefWindowInfo window;
    window.SetAsPopup(nullptr, "SBP download test");
    window.runtime_style = CEF_RUNTIME_STYLE_ALLOY;
    window.style &= ~WS_VISIBLE;
    window.bounds = CefRect(0, 0, 320, 240);

    const CefBrowserSettings settings;
    const auto browser = CefBrowserHost::CreateBrowserSync(
        window, this, pageUrl_, settings, nullptr, nullptr);
    if (!browser) {
        result_.error = "CEF rejected download test browser creation.";
        Complete();
        return;
    }

    // La tarea conserva el cliente, sin retener el navegador; CheckDeadline consultará su ID si sigue activo.
    if (!CefPostDelayedTask(TID_UI, base::BindOnce(&DownloadTestClient::CheckDeadline,
                                                 CefRefPtr<DownloadTestClient>(this)), 5000)) {
        result_.error = "Could not post download test deadline.";
        RequestClose(browser);
    }
}

// CEF confirma la creación en su hilo UI; conserva solo el ID, sin retener el objeto navegador.
void DownloadTestClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browserId_ = browser->GetIdentifier();
    result_.browserCreated = true;
}

// Espera la página principal de la prueba y evita iniciar otra descarga por una notificación repetida.
void DownloadTestClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                   int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_ || downloadRequested_ || !frame || !frame->IsMain()
        || frame->GetURL().ToString() != pageUrl_) {
        return;
    }
    if (httpStatusCode != 200) {
        result_.error = "Download test source page did not return HTTP 200.";
        RequestClose(browser);
        return;
    }

    downloadRequested_ = true;
    // Deja terminar OnLoadEnd antes de activar la descarga, conservando las referencias solo para esta tarea.
    if (!CefPostTask(TID_UI, base::BindOnce(&DownloadTestClient::RequestDownload,
                                          CefRefPtr<DownloadTestClient>(this), browser))) {
        result_.error = "Could not post the download test action.";
        RequestClose(browser);
    }
}

// Inicia el adjunto en el navegador real, sin sustituir los callbacks que CEF emitirá después.
void DownloadTestClient::RequestDownload(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_) {
        return;
    }
    if (trigger_ == DownloadTestTrigger::StartDownload) {
        browser->GetHost()->StartDownload(downloadUrl_);
        return;
    }
    if (trigger_ != DownloadTestTrigger::Link) {
        result_.error = "Unknown download test trigger.";
        RequestClose(browser);
        return;
    }

    // Serializa la URL como argumento JSON para no introducirla directamente entre comillas JavaScript.
    const auto args = CefListValue::Create();
    args->SetString(0, downloadUrl_);
    const auto json = CefValue::Create();
    json->SetList(args);
    const std::string encoded = CefWriteJSON(json, JSON_WRITER_DEFAULT).ToString();
    if (encoded.empty()) {
        result_.error = "Could not serialize download test script arguments.";
        RequestClose(browser);
        return;
    }

    const std::string script =
        "(function(target){const link=document.getElementById('download');"
        "if(!link){throw new Error('Missing download fixture link');}"
        "link.href=target;link.click();}).apply(null," + encoded + ");";
    // La activación se limita a preparar la interacción de prueba, igual que en los casos de historial.
    // La petición HTTP y los callbacks de autorización seguirán pasando por el motor real de CEF.
    const auto parameters = CefDictionaryValue::Create();
    parameters->SetBool("userGesture", true);
    parameters->SetString("expression", script);
    if (browser->GetHost()->ExecuteDevToolsMethod(0, "Runtime.evaluate", parameters) == 0) {
        result_.error = "CEF rejected the activated download test action.";
        RequestClose(browser);
    }
}

// Registra el callback inicial real y conserva exactamente el permiso devuelto por el handler de producción.
bool DownloadTestClient::CanDownload(CefRefPtr<CefBrowser> browser,
                                     const CefString& url, const CefString& requestMethod) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_) {
        return false;
    }
    ++result_.canDownloadCalls;
    const bool allowed = handler_->CanDownload(browser, url, requestMethod);
    if (!allowed) {
        // Este rechazo puede ocurrir antes de existir un CefDownloadItem; no inventa un estado IsCanceled.
        result_.rejectedByCanDownload = true;
        RequestClose(browser);
    }
    return allowed;
}

// Copia el nombre y el ID antes de delegar; nunca conserva CefDownloadItem fuera del callback.
bool DownloadTestClient::OnBeforeDownload(CefRefPtr<CefBrowser> browser,
                                          CefRefPtr<CefDownloadItem> downloadItem,
                                          const CefString& suggestedName,
                                          CefRefPtr<CefBeforeDownloadCallback> callback) {
    CEF_REQUIRE_UI_THREAD();
    if (completionSent_) {
        return true;
    }
    ++result_.beforeDownloadCalls;
    if (closing_) {
        // Asume la gestión y libera la continuación para no iniciar otra solicitud durante el cierre.
        return true;
    }
    if (!downloadItem || !downloadItem->IsValid()) {
        result_.error = "CEF supplied an invalid download item before the test download.";
        RequestClose(browser);
        return true;
    }
    result_.downloadId = downloadItem->GetId();
    result_.suggestedName = suggestedName.ToString();
    return handler_->OnBeforeDownload(browser, downloadItem, suggestedName, std::move(callback));
}

// Observa los estados nativos y delega la cancelación sin convertir una decisión local en una finalización ficticia.
void DownloadTestClient::OnDownloadUpdated(CefRefPtr<CefBrowser> browser,
                                           CefRefPtr<CefDownloadItem> downloadItem,
                                           CefRefPtr<CefDownloadItemCallback> callback) {
    CEF_REQUIRE_UI_THREAD();
    if (completionSent_) {
        if (callback) {
            callback->Cancel();
        }
        return;
    }
    ++result_.downloadUpdateCalls;
    if (!downloadItem || !downloadItem->IsValid()) {
        result_.error = "CEF supplied an invalid download update in the test.";
        handler_->OnDownloadUpdated(browser, downloadItem, std::move(callback));
        RequestClose(browser);
        return;
    }

    // Cancel del handler puede provocar otra actualización: todos los datos se leen antes de delegar.
    const bool completed = downloadItem->IsComplete();
    const bool canceled = downloadItem->IsCanceled();
    const bool interrupted = downloadItem->IsInterrupted();
    result_.downloadId = downloadItem->GetId();
    result_.completed = result_.completed || completed;
    result_.canceled = result_.canceled || canceled;
    result_.interrupted = result_.interrupted || interrupted;
    // Conserva el máximo observado aunque una actualización posterior de cancelación reduzca el contador.
    result_.receivedBytes = (std::max)(result_.receivedBytes, downloadItem->GetReceivedBytes());
    const std::string fullPath = downloadItem->GetFullPath().ToString();
    if (!fullPath.empty()) {
        result_.fullPath = fullPath;
    }

    handler_->OnDownloadUpdated(browser, downloadItem, std::move(callback));
    if (completed || canceled || interrupted) {
        RequestClose(browser);
    }
}

// Un error de la página o la petición no debe confundirse con un bloqueo correcto del motor de seguridad.
void DownloadTestClient::OnLoadError(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame,
                                     ErrorCode errorCode, const CefString& /*errorText*/,
                                     const CefString& failedUrl) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_ || !frame || !frame->IsMain()
        || (errorCode == ERR_ABORTED && downloadRequested_)) {
        return;
    }
    if (failedUrl.ToString() != pageUrl_ && failedUrl.ToString() != downloadUrl_) {
        return;
    }
    result_.error = "Download test load failed with CEF error "
        + std::to_string(static_cast<int>(errorCode)) + ".";
    RequestClose(browser);
}

// Permite solicitar el cierre desde un escenario de prueba sin exponer un navegador al hilo GoogleTest.
void DownloadTestClient::Close() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_ || browserId_ == -1) {
        return;
    }
    const auto browser = CefBrowserHost::GetBrowserByIdentifier(browserId_);
    if (!browser) {
        // Sin confirmación de cierre no se permite continuar hacia CefShutdown con un estado desconocido.
        std::fputs("Download test could not locate its active browser for closure.\n", stderr);
        std::abort();
    }
    RequestClose(browser);
}

// Un escenario sin estado final observado falla y solicita cerrar, en lugar de darse por cancelado por tiempo.
void DownloadTestClient::CheckDeadline() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_) {
        return;
    }
    if (result_.error.empty()) {
        result_.error = "Expected download test event was not observed; CanDownload: "
            + std::to_string(result_.canDownloadCalls) + "; OnBeforeDownload: "
            + std::to_string(result_.beforeDownloadCalls) + "; OnDownloadUpdated: "
            + std::to_string(result_.downloadUpdateCalls) + ".";
    }
    Close();
}

// Encola el cierre fuera del callback actual; closing_ evita solicitudes repetidas por actualizaciones tardías.
void DownloadTestClient::RequestClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || completionSent_) {
        return;
    }
    closing_ = true;
    if (!CefPostTask(TID_UI, base::BindOnce(&DownloadTestClient::CloseBrowser,
                                          CefRefPtr<DownloadTestClient>(this), browser))) {
        if (result_.error.empty()) {
            result_.error = "Could not post download test browser closure.";
        }
        CloseBrowser(browser);
    }
}

// Retira las continuaciones y transferencias del handler antes de cerrar la instancia nativa de prueba.
void DownloadTestClient::CloseBrowser(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    if (completionSent_ || result_.browserClosed) {
        return;
    }
    handler_->CancelAll();
    browser->GetHost()->CloseBrowser(true);
}

// Confirma el cierre y publica el resultado después de salir del último callback del navegador.
void DownloadTestClient::OnBeforeClose(CefRefPtr<CefBrowser> /*browser*/) {
    CEF_REQUIRE_UI_THREAD();
    browserId_ = -1;
    closing_ = true;
    result_.browserClosed = true;
    // También cubre un cierre iniciado directamente por el navegador, fuera de CloseBrowser.
    handler_->CancelAll();
    if (!CefPostTask(TID_UI, base::BindOnce(&DownloadTestClient::Complete,
                                          CefRefPtr<DownloadTestClient>(this)))) {
        std::fputs("Could not post download test completion after browser closure.\n", stderr);
        std::abort();
    }
}

// Cumple la promesa una sola vez: Run devolverá estas copias al hilo de GoogleTest para sus aserciones.
void DownloadTestClient::Complete() {
    CEF_REQUIRE_UI_THREAD();
    if (completionSent_) {
        return;
    }
    if (result_.browserCreated && !result_.browserClosed) {
        std::fputs("Download test completion attempted before browser closure.\n", stderr);
        std::abort();
    }
    completionSent_ = true;
    completion_.set_value(result_);
}

}  // namespace sbp
