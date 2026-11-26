#pragma once

#include "security/security_context.h"
#include "security/security_event.h"

#include <functional>

namespace sbp {

// El receptor recibe el contexto original. Si difiere el envío, debe conservar una copia de ambos argumentos.
// true indica que acepta tramitar el evento. La confirmación del backend llega después.
using SecurityEventSink = std::function<bool(const SecurityContext& origin, const SecurityEvent& event)>;

}  // namespace sbp
