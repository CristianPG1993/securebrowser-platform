# ADR-0004: Relación de los eventos con dispositivos y compañías

- Estado: Aceptada
- Fecha: 2026-10-02

## Contexto

Los eventos de seguridad del MVP se generan en Desktop.

Cada instalación está representada por un Device, que pertenece
a una Company.

Es necesario definir cómo relacionar cada SecurityEvent con su instalación
y cómo determinar la compañía a la que corresponde.

## Opciones consideradas

### Compañía mediante Device

Relacionar cada SecurityEvent con un único Device y obtener su Company
a través de ese dispositivo.

Esta opción aprovecha la relación existente entre Device y Company.

### Compañía explícita en SecurityEvent

Relacionar cada SecurityEvent directamente con Device y Company.

Esta opción permite acceder directamente a la compañía desde el evento,
pero exige mantener la coherencia entre ambas relaciones.

## Decisión

Cada SecurityEvent pertenecerá a un único Device.

Un Device podrá tener cero o muchos SecurityEvents.

La Company de cada evento se obtendrá a través de su Device asociado.

### Concreción para Desktop (2026-11-08)

Conforme al ADR-0015, el receptor de eventos conservará el contexto de sesión e
instalación que produjo el suceso. Un cambio posterior de sesión no reasignará
el evento a otro Device.

El contexto local acompañará la entrega, separado de los cuatro campos REST
del evento. El backend obtendrá el Device de la ruta autorizada y su Company
a través de esa relación; Desktop no enviará propietarios alternativos en el
cuerpo del evento.

## Consecuencias

- El dispositivo identifica la instalación que generó el evento.
- La relación Device–Company determina la compañía del evento.
- Las consultas de eventos por compañía utilizarán esa relación.
- El modelo de dominio y sus diagramas deberán reflejar estas cardinalidades.

## Validación

Se comprobará que:

- Cada evento está asociado a un único Device.
- Un Device puede existir sin eventos y acumular varios eventos.
- La compañía de un evento corresponde a la Company de su Device.
- Cambiar la sesión activa no modifica la instalación de origen de un evento
  pendiente ni añade campos de propietario al cuerpo REST.

## Referencias

- [Modelo de dominio](../architecture/domain-model.md).
- [Persistencia y sincronización de eventos](0003-offline-security-events.md).
- [Contratos de la API REST](../architecture/rest-api-contracts.md).
- [ADR-0015: producción y contexto de origen de eventos](0015-desktop-security-event-production.md).
