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
| UrlRule | Pertenece a una Policy y almacena un dominio normalizado para URL Filtering. |
| DownloadRule | Pertenece a una Policy y almacena una extensión normalizada para Download Control. |

### Atributos de Company

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador de la compañía, generado por la base de datos. |
| `name` | `String` | Sí | Nombre de la compañía. |
| `createdAt` | `Instant` | Sí | Momento de creación de la compañía. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación de la compañía. |

El identificador puede ser `null` antes de guardar una nueva Company.
Una vez persistida, la compañía debe tener un identificador asignado.

Las fechas utilizan `java.time.Instant` para representar instantes absolutos.
Al crear la compañía, `createdAt` y `updatedAt` tienen el mismo valor.
En las modificaciones posteriores se conserva `createdAt` y se actualiza `updatedAt`.

### Atributos de User

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador del usuario, generado por la base de datos. |
| `name` | `String` | Sí | Nombre del usuario para mostrarlo en la aplicación. |
| `email` | `String` | Sí | Email utilizado para iniciar sesión. |
| `passwordHash` | `String` | Sí | Hash de la contraseña generado mediante BCrypt. |
| `role` | `UserRole` | Sí | Rol del usuario: `ADMIN` o `USER`. |
| `company` | `Company` | Sí | Compañía a la que pertenece el usuario. |
| `createdAt` | `Instant` | Sí | Momento de creación del usuario. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación del usuario. |

El identificador puede ser `null` antes de guardar un nuevo User.
Una vez persistido, el usuario debe tener un identificador asignado.

`passwordHash` almacena el hash BCrypt, nunca la contraseña en claro.
`UserRole` es un enum con los valores `ADMIN` y `USER`.
`company` representa la relación con la única Company del usuario en el MVP.

Las fechas utilizan `java.time.Instant`.
Al crear el usuario, `createdAt` y `updatedAt` tienen el mismo valor.
En las modificaciones posteriores se conserva `createdAt` y se actualiza `updatedAt`.

### Atributos de License

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador de la licencia, generado por la base de datos. |
| `company` | `Company` | Sí | Compañía propietaria de la licencia. |
| `maxInstallations` | `int` | Sí | Número máximo de instalaciones permitidas. |
| `createdAt` | `Instant` | Sí | Momento de creación de la licencia. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación de la licencia. |

El identificador puede ser `null` antes de guardar una nueva License.
Una vez persistida, la licencia debe tener un identificador asignado.

La capacidad ocupada se calcula a partir de los dispositivos activos y los
enrollments pendientes vigentes asociados a la licencia, sin almacenar un contador.

Las fechas utilizan `java.time.Instant`.
Al crear la licencia, `createdAt` y `updatedAt` tienen el mismo valor.
En las modificaciones posteriores se conserva `createdAt` y se actualiza `updatedAt`.

### Atributos de EnrollmentToken

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador del enrollment, generado por la base de datos. |
| `license` | `License` | Sí | Licencia cuyo puesto reserva el enrollment. |
| `user` | `User` | Sí | Usuario al que se entrega el token. |
| `tokenHash` | `String` | Sí | Hash del token para almacenarlo de forma segura. |
| `expiresAt` | `Instant` | Sí | Momento de caducidad del token. |
| `usedAt` | `Instant` | No | Momento en que se utilizó el token; inicialmente `null`. |
| `revokedAt` | `Instant` | No | Momento en que se revocó el token; inicialmente `null`. |
| `createdAt` | `Instant` | Sí | Momento de generación del token. |

El identificador puede ser `null` antes de guardar un nuevo EnrollmentToken.
Una vez persistido, el enrollment debe tener un identificador asignado.

El token original se entrega al usuario. En la base de datos se conserva su hash.

El estado se deriva de las fechas, sin almacenar un enum de estado:

- `usedAt` registra si el token ya se utilizó y cuándo.
- `revokedAt` registra si el token se revocó y cuándo.
- `expiresAt` permite determinar si el token ha caducado.
- Un token está pendiente y vigente cuando `usedAt` y `revokedAt` son `null`
  y el instante actual es anterior a `expiresAt`.

Las fechas utilizan `java.time.Instant`.
`usedAt` y `revokedAt` registran los cambios relevantes del ciclo de vida del token.

### Atributos de Device

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador del dispositivo, generado por la base de datos. |
| `deviceIdentifier` | `String` | Sí | Identificador estable de la instalación, generado por Desktop. |
| `name` | `String` | No | Nombre legible para reconocer el dispositivo. |
| `company` | `Company` | Sí | Compañía propietaria del dispositivo. |
| `user` | `User` | Sí | Usuario al que pertenece la instalación. |
| `license` | `License` | Sí | Licencia utilizada por la instalación. |
| `policy` | `Policy` | Sí | Política asignada al dispositivo. |
| `active` | `boolean` | Sí | Indica si la instalación está activa. |
| `createdAt` | `Instant` | Sí | Momento de registro del dispositivo. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación del dispositivo. |
| `lastSeenAt` | `Instant` | No | Momento de la última comunicación con el backend. |

El identificador puede ser `null` antes de guardar un nuevo Device.
Una vez persistido, el dispositivo debe tener un identificador asignado.
`deviceIdentifier` identifica la instalación y es distinto del `id` del backend.

Al registrar el dispositivo, `active` se inicializa a `true`.
Un dispositivo desactivado libera un puesto de su licencia.
Su reactivación debe respetar la capacidad disponible.

Las fechas utilizan `java.time.Instant`.
Al registrar el dispositivo, `createdAt` y `updatedAt` tienen el mismo valor.
En las modificaciones posteriores se conserva `createdAt` y se actualiza `updatedAt`.

`lastSeenAt` registra la última comunicación conocida, sin garantizar que el
dispositivo siga conectado. No se utiliza para liberar puestos automáticamente,
porque una instalación válida puede seguir funcionando offline.

### Atributos de Policy

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador de la política, generado por la base de datos. |
| `name` | `String` | Sí | Nombre de la política. |
| `company` | `Company` | Sí | Compañía propietaria de la política. |
| `urlFilteringEnabled` | `boolean` | Sí | Activa el filtrado de URLs. |
| `urlFilteringMode` | `FilterMode` | Sí | Modo del filtrado de URLs. |
| `urlRules` | `List<UrlRule>` | Sí, puede estar vacía | Reglas de dominios de la política. |
| `downloadControlEnabled` | `boolean` | Sí | Activa el control de descargas. |
| `downloadControlMode` | `FilterMode` | Sí | Modo del control de descargas. |
| `downloadRules` | `List<DownloadRule>` | Sí, puede estar vacía | Reglas de extensiones de la política. |
| `createdAt` | `Instant` | Sí | Momento de creación de la política. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación de la política. |

El identificador puede ser `null` antes de guardar una nueva Policy.
Una vez persistida, la política debe tener un identificador asignado.

La configuración de las dos funcionalidades se representa directamente mediante
los atributos de Policy. `FilterMode` es un enum compartido con los valores
`DENYLIST` y `ALLOWLIST`.

Cada indicador `enabled` determina si se aplica su funcionalidad.
Las colecciones de reglas están presentes y pueden estar vacías.
Los atributos de UrlRule y DownloadRule se detallan en sus apartados de este documento.

Las fechas utilizan `java.time.Instant`.
Al crear la política, `createdAt` y `updatedAt` tienen el mismo valor.
En las modificaciones posteriores se conserva `createdAt` y se actualiza `updatedAt`.

### Atributos de SecurityEvent

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador del evento, generado por la base de datos. |
| `eventUuid` | `UUID` | Sí | Identificador generado por Desktop para reconocer reenvíos del evento. |
| `device` | `Device` | Sí | Instalación que generó el evento. |
| `type` | `SecurityEventType` | Sí | Tipo de evento de seguridad. |
| `details` | `String` | No | Detalle descriptivo del suceso. |
| `occurredAt` | `Instant` | Sí | Momento en que ocurrió el evento, registrado por Desktop. |
| `receivedAt` | `Instant` | Sí | Momento en que el backend registró el evento por primera vez. |

El identificador puede ser `null` antes de guardar un nuevo SecurityEvent.
Una vez persistido, el evento debe tener un identificador asignado.
`eventUuid` utiliza `java.util.UUID` y conserva el mismo valor durante los reenvíos.
Es el identificador denominado `event_uuid` en el flujo de sincronización.

`SecurityEventType` es un enum con los valores `URL_BLOCKED`, `DOWNLOAD_BLOCKED`
y `POLICY_UPDATED`.

La Company del evento se obtiene a través de `device`, conforme al
[ADR-0004](../adr/0004-security-event-ownership.md).

Las fechas utilizan `java.time.Instant` y permiten distinguir la ocurrencia offline
de la recepción posterior. Los eventos se conservan sin editar; al recibir un
reenvío se conserva el registro original, incluido su `receivedAt`.

### Atributos de UrlRule

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador de la regla, generado por la base de datos. |
| `policy` | `Policy` | Sí | Política a la que pertenece la regla. |
| `domain` | `String` | Sí | Dominio normalizado, por ejemplo `example.com`. |

El identificador puede ser `null` antes de guardar una nueva UrlRule.
Una vez persistida, la regla debe tener un identificador asignado.

El modo `DENYLIST` o `ALLOWLIST` lo determina `Policy.urlFilteringMode`.
La Company de la regla se obtiene a través de su Policy.
Los cambios en las reglas de dominios actualizan `Policy.updatedAt`.

### Atributos de DownloadRule

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador de la regla, generado por la base de datos. |
| `policy` | `Policy` | Sí | Política a la que pertenece la regla. |
| `extension` | `String` | Sí | Extensión normalizada en minúsculas y sin punto, por ejemplo `exe`. |

El identificador puede ser `null` antes de guardar una nueva DownloadRule.
Una vez persistida, la regla debe tener un identificador asignado.

El modo `DENYLIST` o `ALLOWLIST` lo determina `Policy.downloadControlMode`.
Todas las reglas de descargas de una Policy se interpretan según ese mismo modo.
La Company de la regla se obtiene a través de su Policy.
Los cambios en las reglas de extensiones actualizan `Policy.updatedAt`.

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

Cada funcionalidad de una Policy tiene un indicador de activación:
`urlFilteringEnabled` para URL Filtering y `downloadControlEnabled` para Download Control.

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

Los atributos, tipos Java, campos obligatorios y opcionales y enums del MVP
quedan definidos en este documento. También se recoge el estado derivado de
EnrollmentToken y el indicador de actividad de Device.

El diseño posterior concretará:

- Las cardinalidades restantes, claves foráneas, restricciones de unicidad,
  longitudes y validaciones de los valores.
- Las reglas de integridad entre Company, User, License, Device y Policy,
  y el comportamiento ante eliminación o desactivación.
- La consistencia del ciclo de vida de EnrollmentToken y las garantías
  transaccionales para reservar, consumir y liberar puestos sin superar la
  capacidad de la licencia, incluidos los reintentos de enrollment.
- El mapeo JPA/PostgreSQL, la estrategia de generación de identificadores
  y el mecanismo de actualización de las fechas de auditoría.
- La generación y el hash de los tokens de enrollment, y los contratos de la API.

Las decisiones arquitectónicas relevantes se documentarán mediante ADR.
