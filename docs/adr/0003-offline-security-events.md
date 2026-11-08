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

### Concreción para el Security Engine (2026-11-08)

El ADR-0015 concreta la producción de eventos al aplicar operaciones y su entrega
a un receptor inyectado. El motor no mantendrá una cola interna; la persistencia
SQLite y el transporte HTTP corresponderán a los bloques Offline y REST.

`POLICY_UPDATED` representará una sustitución correctamente aplicada con cambio
de identidad o configuración. La primera política de la sesión, una renovación
sin cambios y las modificaciones exclusivas de metadatos no producirán el evento.

La API denomina `eventUuid` al identificador del evento. Los reenvíos conservarán
también tipo, `occurredAt` y el contenido serializado de `details`, junto con la
instalación de origen. No se reconstruirán con el contexto activo posterior.

El protocolo de confirmación se recoge en el ADR-0010 y en los contratos REST:
Desktop retirará de SQLite únicamente los UUID incluidos en `acceptedEventUuids`.
Ante un error o pérdida de respuesta conservará los pendientes para reenviarlos
con el mismo contenido. La implementación de SQLite y los reintentos en Desktop
siguen pendientes en su bloque correspondiente.

## Consecuencias

- El registro de eventos soportará periodos sin conexión.
- Los eventos pendientes tendrán persistencia local.
- El cliente y el backend deberán conservar y utilizar el identificador
  del evento durante la sincronización.
- El protocolo de confirmación por UUID ya está definido; su integración,
  la gestión de reintentos y la conservación local requieren el bloque Offline.
- Una captura de eventos en pruebas del motor no acredita persistencia durable
  ni sincronización con el backend.

## Validación

Durante el desarrollo se comprobará que:

- Un evento generado offline queda almacenado en SQLite.
- Los eventos pendientes siguen disponibles al reiniciar Desktop.
- Los eventos pueden sincronizarse al recuperar la conexión.
- Reenviar un evento con el mismo `event_uuid` no crea otro registro
  del mismo evento en el backend.
- Los reenvíos conservan contenido e instalación de origen aunque cambie la
  sesión activa; solo los UUID confirmados se retiran de la cola.

## Referencias

- [Arquitectura general](../architecture/overview.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [Contratos de la API REST](../architecture/rest-api-contracts.md).
- [ADR-0010: protocolo REST y confirmación de lotes](0010-rest-api-contracts.md).
- [ADR-0015: producción y entrega de eventos](0015-desktop-security-event-production.md).
