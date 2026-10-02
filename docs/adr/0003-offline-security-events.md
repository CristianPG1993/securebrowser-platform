# ADR-0003: Persistencia local y sincronización de eventos de seguridad

- Estado: Aceptada
- Fecha: 2026-10-02

## Contexto

La aplicación Desktop debe registrar eventos de seguridad incluso
cuando no tenga conexión con el backend.

Los tipos de evento previstos para el MVP son:

- `URL_BLOCKED`.
- `DOWNLOAD_BLOCKED`.
- `POLICY_UPDATED`.

La sincronización puede reenviar un evento si una comunicación falla.
El backend debe evitar que esos reenvíos generen registros duplicados.

## Opciones consideradas

Se compara la solución acordada con una alternativa basada únicamente
en el envío online.

### Envío únicamente online

Enviar los eventos directamente al backend, sin persistencia local
de los eventos pendientes.

Esta opción no cubre el funcionamiento offline requerido.

### Persistencia local y sincronización posterior

Guardar los eventos pendientes en SQLite y sincronizarlos con el backend
cuando haya conexión.

Utilizar un identificador estable por evento para reconocer los reenvíos.

## Decisión

Desktop almacenará los eventos pendientes de sincronización en SQLite.

El cliente generará un `event_uuid` para cada evento. Ese identificador
se conservará durante su almacenamiento y sus intentos de sincronización.

El backend utilizará `event_uuid` para evitar registros duplicados
cuando reciba varias veces el mismo evento.

El evento `POLICY_UPDATED` se generará en Desktop cuando un dispositivo
aplique correctamente una actualización de su política. Representará
la aplicación efectiva del cambio en ese dispositivo.

## Consecuencias

- El registro de eventos soportará periodos sin conexión.
- Los eventos pendientes tendrán persistencia local.
- El cliente y el backend deberán conservar y utilizar el identificador
  del evento durante la sincronización.
- Será necesario concretar el protocolo de confirmación, los reintentos
  y la conservación de los eventos locales.

## Validación

Durante el desarrollo se comprobará que:

- Un evento generado offline queda almacenado en SQLite.
- Los eventos pendientes siguen disponibles al reiniciar Desktop.
- Los eventos pueden sincronizarse al recuperar la conexión.
- Reenviar un evento con el mismo `event_uuid` no crea otro registro
  del mismo evento en el backend.

## Referencias

- [Arquitectura general](../architecture/overview.md).
- [Modelo de dominio](../architecture/domain-model.md).


El identificador representa al **evento**, por eso se mantiene cuando se reenvía. El protocolo concreto de sincronización queda pendiente.