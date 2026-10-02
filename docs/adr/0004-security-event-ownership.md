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

## Referencias

- [Modelo de dominio](../architecture/domain-model.md).
- [Persistencia y sincronización de eventos](0003-offline-security-events.md).