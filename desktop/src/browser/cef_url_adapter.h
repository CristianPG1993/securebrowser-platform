#pragma once

#include "security/url_target.h"

#include "include/internal/cef_string.h"

#include <string>

namespace sbp {

// Traduce una URL de CEF a los datos simples que recibe el evaluador de seguridad.
// Pertenece a la integración del navegador; el núcleo no depende de CEF.
class CefUrlAdapter final {
public:
    // Interpreta con CefParseURL y prepara esquema, host y clasificación dominio/IP.
    // Devuelve un destino marcado como inválido cuando no se puede interpretar.
    // No consulta políticas, autoriza navegaciones ni produce eventos.
    [[nodiscard]] UrlTarget Parse(const CefString& url) const;

private:
    // Recibe exclusivamente el componente host de una URL interpretada correctamente por CEF.
    // Reconoce direcciones numéricas sin resolver nombres ni realizar peticiones de red.
    [[nodiscard]] static UrlHostKind ClassifyHost(const std::string& host);
};

}  // namespace sbp
