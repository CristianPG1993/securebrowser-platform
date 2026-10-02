# Modelo de dominio de SecureBrowser Platform

## Alcance

Este documento recoge las entidades y reglas acordadas para el MVP.

## Diagrama conceptual

![Modelo de dominio de SBP](../../diagrams/domain-model.svg)

[Fuente editable del diagrama (Mermaid)](../../diagrams/domain-model.mmd).

El diagrama resume las asociaciones del MVP. Las relaciones Company–User
y Device–SecurityEvent incluyen las cardinalidades acordadas. La nota
de Company recoge la obligación de conservar al menos un ADMIN.
Las cardinalidades completas de las demás asociaciones se concretarán
al detallar el dominio.

## Entidades

| Entidad | Responsabilidad y relaciones |
| --- | --- |
| Company | Agrupa usuarios, licencias, políticas y dispositivos. |
| User | Pertenece a una única Company y tiene el rol ADMIN o USER. |
| License | Pertenece a una Company y limita el número de instalaciones. |
| EnrollmentToken | Se asocia a una License y a un User para autorizar el enrollment. |
| Device | Representa una instalación, pertenece a una Company y a un User, utiliza una License y siempre tiene una Policy. |
| Policy | Pertenece a una Company y puede asignarse a varios Devices. |
| SecurityEvent | Pertenece a un único Device. Su Company se obtiene a través de ese Device. Registra sucesos de seguridad y permite su sincronización tras trabajar offline. |

## Usuarios y administración

- Cada Company tiene uno o muchos Users y se crea con su primer ADMIN.
- Cada Company debe conservar al menos un User con el rol ADMIN.
- Se impide eliminar o cambiar a USER el rol del último ADMIN de una Company.
- El inicio de sesión utiliza email y contraseña.
- Las contraseñas se almacenan mediante BCrypt.
- Cada ADMIN administra únicamente su propia Company.

## Licencias y enrollment

- Los enrollments pendientes reservan puestos de la License.
- Los dispositivos desactivados liberan puestos.
- El backend genera el EnrollmentToken y lo envía por email.
- El token es de un solo uso, tiene caducidad y puede revocarse.
- Se almacena de forma segura, sin guardar el token reutilizable en claro.

## Dispositivos

- Cada Device representa una instalación de la aplicación.
- La aplicación genera su identificador estable.
- Cada Device debe tener una Policy asignada.

## Políticas de seguridad

Cada funcionalidad de una Policy tiene una propiedad `enabled`.

### URL Filtering

- Utiliza el modo DENYLIST o ALLOWLIST.
- Las reglas se basan en dominios normalizados.
- `example.com` coincide con `www.example.com`.
- `example.com` no coincide con `example.com.evil.com`.
- Una URL HTTP/HTTPS inválida debe bloquearse de forma segura.

### Download Control

- Utiliza el modo DENYLIST o ALLOWLIST.
- Las extensiones se normalizan en minúsculas y sin el punto inicial.
- Se utiliza la última extensión del archivo.
- Los archivos sin extensión se bloquean en el MVP.

## Eventos de seguridad


Un Device puede tener cero o muchos SecurityEvents. Cada evento corresponde
a una única instalación.

Los tipos previstos son:

- `URL_BLOCKED`.
- `DOWNLOAD_BLOCKED`.
- `POLICY_UPDATED`: generado por Desktop cuando un dispositivo aplica
  correctamente una actualización de su política.

El sistema debe soportar funcionamiento offline:

- Los eventos pendientes se almacenan localmente en SQLite.
- Cada evento lleva un `event_uuid` generado por el cliente.
- Este identificador permite evitar duplicados durante la sincronización.

## Detalles pendientes

El diseño posterior concretará los atributos de las entidades, sus estados,
las restricciones de persistencia y los contratos de la API.

Las decisiones arquitectónicas relevantes se documentarán mediante ADR.