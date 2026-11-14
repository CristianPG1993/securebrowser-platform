// Winsock debe incluirse antes de las cabeceras que puedan incorporar windows.h.
#include <winsock2.h>
#include <ws2tcpip.h>

#include "browser/cef_url_adapter.h"

#include "include/cef_parser.h"

#include <utility>

namespace sbp {

// Obtiene los componentes de Chromium y conserva únicamente los datos que necesita el motor.
UrlTarget CefUrlAdapter::Parse(const CefString& url) const {
    // El wrapper de CEF exige una cadena no vacía también en sus comprobaciones de Debug.
    if (url.empty()) {
        return UrlTarget(false, "", "", UrlHostKind::Unknown);
    }

    CefURLParts parts;
    if (!CefParseURL(url, parts)) {
        return UrlTarget(false, "", "", UrlHostKind::Unknown);
    }

    std::string scheme = CefString(&parts.scheme).ToString();
    std::string host = CefString(&parts.host).ToString();
    if (scheme.empty() || scheme.find('\0') != std::string::npos
        || host.find('\0') != std::string::npos) {
        return UrlTarget(false, "", "", UrlHostKind::Unknown);
    }

    // Clasifica antes de retirar el punto final para conservar la distinción entre dominio e IP.
    const UrlHostKind hostKind = ClassifyHost(host);

    for (char& character : scheme) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }

    for (char& character : host) {
        // El host web debe llegar convertido a ASCII por Chromium; no se añaden conversiones IDN propias.
        if (static_cast<unsigned char>(character) > 0x7F) {
            return UrlTarget(false, "", "", UrlHostKind::Unknown);
        }
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    if (!host.empty() && host.back() == '.') {
        host.pop_back();
    }

    // Una interpretación correcta no implica que el esquema o el destino estén permitidos.
    return UrlTarget(true, std::move(scheme), std::move(host), hostKind);
}

// Clasifica el componente obtenido por CEF; no interpreta de nuevo la URL completa.
UrlHostKind CefUrlAdapter::ClassifyHost(const std::string& host) {
    if (host.empty() || host.find('\0') != std::string::npos) {
        return UrlHostKind::Unknown;
    }

    // CefURLParts representa IPv6 entre corchetes; InetPtonA recibe solo la dirección.
    if (host.front() == '[' || host.back() == ']') {
        if (host.size() <= 2 || host.front() != '[' || host.back() != ']') {
            return UrlHostKind::Unknown;
        }
        const std::string address = host.substr(1, host.size() - 2);
        IN6_ADDR ipv6{};
        return InetPtonA(AF_INET6, address.c_str(), &ipv6) == 1
            ? UrlHostKind::Ipv6 : UrlHostKind::Unknown;
    }

    // Un host con dos puntos que no tenga la representación IPv6 esperada no es un dominio.
    if (host.find(':') != std::string::npos) {
        return UrlHostKind::Unknown;
    }

    // Un punto final no debe convertir una dirección IPv4 en una regla de dominio.
    std::string address = host;
    if (address.back() == '.') {
        address.pop_back();
    }
    IN_ADDR ipv4{};
    if (InetPtonA(AF_INET, address.c_str(), &ipv4) == 1) {
        return UrlHostKind::Ipv4;
    }

    // La validez sintáctica y la canonicalización del host corresponden a CefParseURL.
    return UrlHostKind::Domain;
}

}  // namespace sbp
