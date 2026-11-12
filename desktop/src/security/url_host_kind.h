#pragma once

namespace sbp {

// Clasifica el host interpretado por el adaptador, sin analizar una URL en el núcleo.
enum class UrlHostKind {
    // El adaptador todavía no ha identificado un tipo de host válido.
    Unknown,

    // Nombre de dominio preparado en ASCII para comparar con las reglas.
    Domain,

    // Dirección IPv4 literal, que no se compara por sufijos de dominio.
    Ipv4,

    // Dirección IPv6 literal, que no se compara por sufijos de dominio.
    Ipv6
};

}  // namespace sbp
