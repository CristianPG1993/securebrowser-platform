# Modelo de dominio de SecureBrowser Platform

## Alcance

Este documento recoge las entidades y reglas acordadas para el MVP.

SBP está orientado a empresas. Todo User pertenece a una Company y todas las
políticas son corporativas: el ADMIN las configura y las asigna a los Devices.
El MVP no incluye usuarios particulares sin Company ni políticas personales.

Las decisiones de integridad y concurrencia se recogen en el
[ADR-0006](../adr/0006-domain-integrity-and-enrollment.md).
La renovación automática y la entidad técnica RefreshToken se definen en el
[ADR-0008](../adr/0008-jwt-authentication.md), dentro de la tarea #6.

## Diagrama conceptual

![Modelo de dominio de SBP](../../diagrams/domain-model.svg)

[Fuente editable del diagrama (Mermaid)](../../diagrams/domain-model.mmd).

El diagrama resume las asociaciones del MVP. La nota de Company recoge
la obligación de conservar al menos un ADMIN. Las cardinalidades completas
se detallan en el apartado [Cardinalidades](#cardinalidades).

## Entidades

| Entidad | Responsabilidad y relaciones |
| --- | --- |
| Company | Agrupa usuarios, licencias, políticas y dispositivos. |
| User | Pertenece a una única Company y tiene el rol ADMIN o USER. |
| License | Pertenece a una Company y limita el número de instalaciones. |
| EnrollmentToken | Se asocia a una License, un User y una Policy para reservar un puesto y autorizar el enrollment con su política inicial. |
| Device | Representa una instalación creada por un EnrollmentToken, pertenece a una Company y a un User, utiliza una License y siempre tiene una Policy. |
| Policy | Pertenece a una Company y puede asignarse a varios Devices. |
| SecurityEvent | Pertenece a un único Device. Su Company se obtiene a través de ese Device. Registra sucesos de seguridad y permite su sincronización tras trabajar offline. |
| UrlRule | Pertenece a una Policy y almacena un dominio normalizado para URL Filtering. |
| DownloadRule | Pertenece a una Policy y almacena una extensión normalizada para Download Control. |
| RefreshToken | Pertenece a un User y permite renovar su autenticación. Sus registros se agrupan por login mediante familyId, con rotación y revocación. |

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
| `name` | `String` | Sí | Nombre de pila del usuario. |
| `lastName` | `String` | Sí | Apellido o apellidos del usuario. |
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
| `policy` | `Policy` | Sí | Política inicial seleccionada por el ADMIN al emitir el enrollment. |
| `tokenHash` | `String` | Sí | Hash del token para almacenarlo de forma segura. |
| `expiresAt` | `Instant` | Sí | Momento de caducidad del token. |
| `usedAt` | `Instant` | No | Momento en que se utilizó el token; inicialmente `null`. |
| `revokedAt` | `Instant` | No | Momento en que se revocó el token; inicialmente `null`. |
| `createdAt` | `Instant` | Sí | Momento de generación del token. |

El identificador puede ser `null` antes de guardar un nuevo EnrollmentToken.
Una vez persistido, el enrollment debe tener un identificador asignado.

El token original se entrega al usuario. En la base de datos se conserva su hash.
User, License y Policy deben pertenecer a la misma Company. La política indicada
por el enrollment se asigna al Device cuando se registra la instalación.

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
| `enrollmentToken` | `EnrollmentToken` | Sí | Enrollment de origen; se conserva para reconocer reintentos del registro. |
| `active` | `boolean` | Sí | Indica si la instalación está activa. |
| `createdAt` | `Instant` | Sí | Momento de registro del dispositivo. |
| `updatedAt` | `Instant` | Sí | Momento de la última modificación del dispositivo. |
| `lastSeenAt` | `Instant` | No | Momento de la última comunicación con el backend. |

El identificador puede ser `null` antes de guardar un nuevo Device.
Una vez persistido, el dispositivo debe tener un identificador asignado.
`deviceIdentifier` identifica la instalación y es distinto del `id` del backend.
Desktop genera un UUID y conserva su representación canónica en este atributo.
Cada Device procede de un único enrollment y cada enrollment puede crear como
máximo un Device. El identificador de instalación no es una credencial.

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

### Atributos de RefreshToken

| Atributo | Tipo Java | Obligatorio al persistir | Descripción |
| --- | --- | --- | --- |
| `id` | `Long` | Sí | Identificador interno generado por la base de datos. |
| `user` | `User` | Sí | Usuario propietario de la renovación. |
| `tokenHash` | `String` | Sí | SHA-256 del secreto de renovación, sin guardar el token original. |
| `familyId` | `UUID` | Sí | Identificador del grupo creado en un login; se conserva en sus renovaciones. |
| `createdAt` | `Instant` | Sí | Momento de creación de este refresh token. |
| `expiresAt` | `Instant` | Sí | Caducidad del grupo, conservada al rotar. |
| `usedAt` | `Instant` | No | Momento de consumo durante una renovación; inicialmente null. |
| `revokedAt` | `Instant` | No | Momento de revocación; inicialmente null. |

Es una entidad técnica de autenticación añadida en la tarea #6. No reserva
puestos de License ni sustituye a EnrollmentToken. Su Company se obtiene
a través de User y no exige una relación con Device.

Cada login genera un familyId nuevo y una caducidad inicial de 7 días,
configurable. Los tokens sucesivos del grupo conservan User, familyId y expiresAt.
Un token puede renovar cuando usedAt y revokedAt son null y aún no ha caducado.

El estado se deriva de las fechas. Un registro consumido puede ser posteriormente
revocado junto con su grupo, por lo que usedAt y revokedAt pueden coexistir.
Los registros consumidos se conservan mientras el grupo pueda seguir vigente.

## Cardinalidades

| Relación | Cardinalidad |
| --- | --- |
| Company → User | `1 → 1..*` |
| Company → License | `1 → 0..*` |
| Company → Policy | `1 → 0..*` |
| Company → Device | `1 → 0..*` |
| User → Device | `1 → 0..*` |
| License → Device | `1 → 0..*` |
| Policy → Device | `1 → 0..*` |
| User → EnrollmentToken | `1 → 0..*` |
| License → EnrollmentToken | `1 → 0..*` |
| Policy → EnrollmentToken | `1 → 0..*` |
| EnrollmentToken → Device | `1 → 0..1` |
| Device → SecurityEvent | `1 → 0..*` |
| Policy → UrlRule | `1 → 0..*` |
| Policy → DownloadRule | `1 → 0..*` |
| User → RefreshToken | `1 → 0..*` |

Cada entidad de la derecha pertenece a exactamente una entidad de la izquierda.
`0..*` permite que la colección comience vacía y tenga varios elementos.
`1..*` exige al menos un elemento. `0..1` permite que un enrollment aún no haya
creado un Device, pero impide que cree varias instalaciones.

Company se crea con su primer User ADMIN y debe conservar al menos uno.
Sus colecciones de licencias, políticas y dispositivos pueden comenzar vacías.

La relación License–Device permite conservar dispositivos desactivados.
El límite de capacidad se aplica a los dispositivos activos más los enrollments
pendientes vigentes, en lugar de limitar el número total de registros de Device.

## Restricciones de integridad

### Company

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `name` | Obligatorio, máximo 150 caracteres y con contenido después de quitar los espacios exteriores. |
| `createdAt` | Obligatorio; lo asigna el backend al crear la compañía y se conserva. |
| `updatedAt` | Obligatorio; lo asigna y actualiza el backend. |

El nombre puede repetirse entre compañías. El identificador único de Company
es `id`.

La compañía se crea con su primer User ADMIN y debe conservar al menos uno.
Las operaciones que afecten a sus ADMIN deben respetar las restricciones de User.

### User

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `name` | Obligatorio, máximo 150 caracteres y con contenido después de quitar los espacios exteriores. Puede repetirse. |
| `lastName` | Obligatorio, máximo 150 caracteres y con contenido después de quitar los espacios exteriores. Puede repetirse. |
| `email` | Obligatorio, formato válido y máximo 254 caracteres. Se normaliza sin espacios exteriores y en minúsculas, y tiene una restricción `UNIQUE` global. |
| `passwordHash` | Obligatorio, generado mediante BCrypt por el backend y máximo 255 caracteres. |
| `role` | Obligatorio; únicamente `ADMIN` o `USER`. |
| `company` | Obligatoria; clave foránea `company_id` hacia `Company.id`. |
| `createdAt` | Obligatorio; lo asigna el backend al crear el usuario y se conserva. |
| `updatedAt` | Obligatorio; lo asigna y actualiza el backend. |

El email normalizado identifica de forma única al usuario para iniciar sesión,
independientemente de su Company. La unicidad se garantiza también en la base de
datos. Las relaciones entre entidades utilizan `User.id`.

Se impide eliminar o cambiar a USER el rol del último ADMIN de una Company,
también ante peticiones simultáneas. Las operaciones bloquean primero la fila
de Company y comprueban los ADMIN restantes dentro de la misma transacción.

### License

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `company` | Obligatoria; clave foránea `company_id` hacia `Company.id`. |
| `maxInstallations` | Obligatorio; número entero mayor o igual que `1`. |
| `createdAt` | Obligatorio; lo asigna el backend al crear la licencia y se conserva. |
| `updatedAt` | Obligatorio; lo asigna y actualiza el backend. |

La ocupación de la licencia debe mantenerse dentro de `maxInstallations`.
Se calcula mediante sus dispositivos activos y sus enrollments pendientes vigentes.

Para reducir `maxInstallations`, el nuevo valor debe ser mayor o igual que la
ocupación actual. La comprobación y la modificación deben respetar esta regla
también ante operaciones simultáneas, mediante el bloqueo de License descrito
en el apartado de garantías transaccionales.

### EnrollmentToken

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `license` | Obligatoria; clave foránea `license_id` hacia `License.id`. |
| `user` | Obligatorio; clave foránea `user_id` hacia `User.id`. |
| `policy` | Obligatoria; clave foránea `policy_id` hacia `Policy.id`. |
| `tokenHash` | Obligatorio; hash SHA-256 de 64 caracteres hexadecimales, con `UNIQUE` global. |
| `createdAt` | Obligatorio; lo asigna el backend al emitir el token y se conserva. |
| `expiresAt` | Obligatorio y posterior a `createdAt`. |
| `usedAt` | Opcional; si existe, debe cumplirse `createdAt <= usedAt < expiresAt`. |
| `revokedAt` | Opcional; si existe, debe ser mayor o igual que `createdAt`. |

Los valores de `usedAt` y `revokedAt` no pueden estar presentes a la vez.
Un token utilizado no se revoca y un token revocado no se consume.
Las restricciones entre fechas y la exclusión entre utilización y revocación
se expresarán también mediante `CHECK` en PostgreSQL.

Solo el primer consumo de un token pendiente y vigente puede crear un Device.
La comparación con el instante actual se realiza en el servicio; la caducidad
no depende de modificar el registro ni de ejecutar una tarea periódica.

### Device

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `deviceIdentifier` | Obligatorio; UUID canónico de 36 caracteres generado por Desktop, con `UNIQUE` global. |
| `name` | Opcional, máximo 150 caracteres; se quitan los espacios exteriores y un valor vacío se guarda como `null`. |
| `company` | Obligatoria; clave foránea `company_id` hacia `Company.id`. |
| `user` | Obligatorio; clave foránea `user_id` hacia `User.id`. |
| `license` | Obligatoria; clave foránea `license_id` hacia `License.id`. |
| `policy` | Obligatoria; clave foránea `policy_id` hacia `Policy.id`. |
| `enrollmentToken` | Obligatorio; clave foránea `enrollment_token_id` hacia `EnrollmentToken.id`, con `UNIQUE`. |
| `active` | Obligatorio; inicialmente `true`. |
| `createdAt` | Obligatorio; lo asigna el backend al registrar el Device y se conserva. |
| `updatedAt` | Obligatorio; lo asigna y actualiza el backend. |
| `lastSeenAt` | Opcional; lo asigna el backend al recibir una comunicación autenticada del Device. |

El Device se registra para el usuario y la licencia del enrollment, utilizando
su política inicial. Todas las relaciones corresponden a la misma Company.
Una política distinta puede asignarse posteriormente dentro de esa Company,
sin modificar la política de origen conservada en EnrollmentToken.

Un Device inactivo no ocupa un puesto. Reactivarlo exige capacidad disponible
y repetir un enrollment utilizado nunca lo reactiva automáticamente.

### Policy

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `name` | Obligatorio, máximo 150 caracteres y con contenido después de quitar los espacios exteriores. Puede repetirse. |
| `company` | Obligatoria; clave foránea `company_id` hacia `Company.id`. |
| `urlFilteringEnabled`, `downloadControlEnabled` | Obligatorios; inicialmente `true`. |
| `urlFilteringMode`, `downloadControlMode` | Obligatorios; solo `DENYLIST` o `ALLOWLIST`, incluso con la funcionalidad desactivada. Inicialmente `DENYLIST`. |
| `urlRules`, `downloadRules` | Colecciones presentes; inicialmente vacías. La pertenencia se persiste mediante la clave foránea de cada regla. |
| `createdAt` | Obligatorio; lo asigna el backend al crear la política y se conserva. |
| `updatedAt` | Obligatorio; lo asigna y actualiza el backend, también cuando cambian sus reglas. |

Una DENYLIST vacía no bloquea elementos por pertenecer a la lista. Una ALLOWLIST
vacía bloquea todos los elementos sometidos a esa funcionalidad. Se mantienen
las reglas de validación segura de URLs y de archivos sin extensión.

La edición de la política y de sus reglas se confirma en una única transacción.
Las modificaciones concurrentes de esa configuración se coordinan bloqueando
la fila de Policy antes de modificarla.

### SecurityEvent

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `eventUuid` | UUID obligatorio, generado por Desktop, con `UNIQUE` global. |
| `device` | Obligatorio; clave foránea `device_id` hacia `Device.id`. |
| `type` | Obligatorio; solo `URL_BLOCKED`, `DOWNLOAD_BLOCKED` o `POLICY_UPDATED`. |
| `details` | Opcional, máximo 2.000 caracteres. |
| `occurredAt` | Obligatorio; instante registrado por Desktop. |
| `receivedAt` | Obligatorio; lo asigna el backend al registrar por primera vez el evento. |

No se exige `occurredAt <= receivedAt`, porque el reloj de Desktop puede estar
desajustado. Las fechas de cliente no determinan la vigencia de los enrollments.

Un reenvío con el mismo UUID, Device, tipo, detalle e instante de ocurrencia
conserva el registro original y su `receivedAt`. Reutilizar el UUID con un Device
o contenido diferente se rechaza como conflicto. La restricción de unicidad
debe garantizar la deduplicación también ante recepciones simultáneas.

Los eventos son inmutables. Un Device desactivado puede sincronizar eventos
pendientes sin reactivarse ni volver a ocupar un puesto.

### UrlRule

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `policy` | Obligatoria; clave foránea `policy_id` hacia `Policy.id`. |
| `domain` | Obligatorio; dominio válido normalizado, máximo 253 caracteres en su representación ASCII. |

Se eliminan los espacios exteriores y el punto final del dominio, se utilizan
minúsculas y los dominios internacionales se convierten a su representación
ASCII. Se rechazan esquemas, rutas, puertos y comodines.

La restricción `UNIQUE(policy_id, domain)` impide repetir un dominio normalizado
en una misma Policy. El mismo dominio puede aparecer en políticas diferentes.

### DownloadRule

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `policy` | Obligatoria; clave foránea `policy_id` hacia `Policy.id`. |
| `extension` | Obligatoria, máximo 20 caracteres; normalizada en minúsculas y sin punto inicial. |

Se rechazan extensiones vacías, espacios, rutas y puntos interiores.
La restricción `UNIQUE(policy_id, extension)` impide repetir una extensión
normalizada en una misma Policy. Puede aparecer en políticas diferentes.

### RefreshToken

| Atributo | Restricción |
| --- | --- |
| `id` | Clave primaria, obligatoria y generada por la base de datos. |
| `user` | Obligatorio; clave foránea `user_id` hacia `User.id`. |
| `tokenHash` | Obligatorio; SHA-256 de 64 caracteres hexadecimales, con UNIQUE global. |
| `familyId` | UUID obligatorio generado por el backend al iniciar el grupo. Se repite en los tokens de ese grupo. |
| `createdAt` | Obligatorio; lo asigna el backend y se conserva. |
| `expiresAt` | Obligatorio y posterior a createdAt; se conserva al rotar. |
| `usedAt` | Opcional; si existe, debe cumplirse createdAt <= usedAt < expiresAt. |
| `revokedAt` | Opcional; si existe, debe ser mayor o igual que createdAt. |

Las restricciones temporales de cada fila se expresan también mediante CHECK.
El servicio conserva el mismo usuario y caducidad para todos los tokens de
un familyId. Al revocar un grupo se revocan sus registros, incluidos los ya
consumidos; usedAt y revokedAt no son excluyentes en RefreshToken.

La generación utiliza 32 bytes de SecureRandom codificados como Base64 URL-safe.
El secreto se entrega al cliente; no se guarda en PostgreSQL ni se registra en
logs. Las reglas de consumo, rotación y revocación se detallan en el
[diseño de autenticación](jwt-authentication.md).

## Pertenencia e integridad entre compañías

Se conservan durante toda la vida del registro estas asociaciones:

- User, License y Policy con su Company.
- Device con su Company, User, License y EnrollmentToken de origen.
- EnrollmentToken con su User, License y Policy inicial.
- UrlRule y DownloadRule con su Policy.
- SecurityEvent con su Device.
- RefreshToken con su User y el grupo familyId de origen.

La política actual de un Device sí puede cambiar, siempre dentro de su Company.
El servicio verifica todas las pertenencias antes de persistir las asociaciones.

PostgreSQL garantiza claves primarias, claves foráneas, `NOT NULL`, unicidad y
las restricciones `CHECK` de cada fila. Las claves foráneas independientes no
comprueban que las entidades relacionadas pertenezcan a la misma Company:
esta igualdad y la inmutabilidad de pertenencia se garantizan en los servicios
transaccionales del MVP, sin añadir triggers ni relaciones compuestas.

User, License, Policy y Device conservan sus referencias explícitas a Company.
SecurityEvent obtiene su Company a través de Device y las reglas a través de
Policy, sin duplicar esas referencias.
RefreshToken obtiene su Company a través de User; el cliente no selecciona
otro propietario ni compañía al renovar.

## Eliminación y desactivación

| Recurso | Comportamiento del MVP |
| --- | --- |
| Company | No se elimina. |
| User | Solo se elimina si no tiene Devices ni enrollments y no es el último ADMIN. El borrado permitido elimina en cascada sus RefreshTokens. |
| License | Solo se elimina si no tiene Devices ni enrollments asociados. |
| Policy | Solo se elimina si ningún Device ni enrollment la referencia; sus reglas se eliminan con ella. |
| Device | Se desactiva; se conserva el registro y su historial. |
| EnrollmentToken | Se revoca o caduca; se conserva el registro. |
| SecurityEvent | Se conserva sin edición ni eliminación individual. |
| UrlRule y DownloadRule | Se pueden eliminar individualmente o junto con su Policy. |
| RefreshToken | Se conserva mientras su grupo pueda seguir vigente; puede eliminarse tras caducar el grupo y se elimina en cascada al borrar su User. |

Una referencia histórica impide eliminar el recurso aunque el Device esté
inactivo o el enrollment haya caducado. Las claves foráneas bloquean esos
borrados mediante `RESTRICT` o `NO ACTION`. Las reglas de Policy y los tokens
técnicos de renovación de un User eliminado utilizan cascada de eliminación;
no se propaga el borrado a usuarios, licencias, dispositivos, enrollments ni eventos.

## Garantías transaccionales

### Capacidad de License

La ocupación es la suma de Devices activos y EnrollmentTokens pendientes
vigentes asociados a la licencia. No se almacena un contador persistente.

Cada operación que modifica esa ocupación o el límite de capacidad utiliza
una transacción y bloquea primero la fila de License mediante `SELECT FOR UPDATE`
o su equivalente JPA. Con el bloqueo adquirido, obtiene el instante actual,
vuelve a consultar los registros pertinentes, valida la capacidad y aplica
los cambios antes de confirmar.

Este procedimiento se aplica a:

- Generar, consumir o revocar enrollments.
- Activar o desactivar Devices.
- Modificar `maxInstallations`.

Al consumir un token se crea el Device activo y se asigna `usedAt` en la misma
transacción. La reserva deja de contar al convertirse en instalación, sin sumar
un segundo puesto. Si la transacción falla, se revierten ambos cambios.

Los tokens caducados y revocados no reservan puestos. Desactivar un Device
libera su puesto; reactivarlo exige que quede capacidad. Reducir la licencia
exige que el nuevo límite no sea inferior a su ocupación actual.

### Reintentos de enrollment

Antes del primer consumo se valida que el usuario autenticado sea el destinatario
y que el token esté pendiente y vigente. El servicio verifica además la Company
y las asociaciones del enrollment. Desktop no selecciona otra política.

Si el enrollment ya está utilizado, se busca su Device de origen. Una petición
del mismo usuario con el mismo `deviceIdentifier` devuelve ese Device existente,
sin modificar `usedAt`, crear otra instalación, cambiar la política actual ni
reactivar un Device inactivo. La caducidad posterior del token no impide reconocer
este reintento autenticado del registro ya completado.

Un identificador de instalación diferente se rechaza. Tampoco se permite crear
otro Device con un `deviceIdentifier` existente utilizando otro enrollment.
Las restricciones únicas de `enrollment_token_id` y `deviceIdentifier` protegen
estas reglas también ante peticiones simultáneas.

### Company y último ADMIN

Company y su primer User ADMIN se crean en una misma transacción. Si cualquiera
de los registros falla, no se conserva una compañía sin ADMIN.

Cambiar roles o eliminar usuarios bloquea primero la fila de Company y comprueba
la cantidad de ADMIN dentro de esa transacción. Dos peticiones simultáneas no
pueden eliminar o degradar por separado a los dos últimos administradores.

### Duración y orden de los bloqueos

Las operaciones comparten un orden de adquisición de bloqueos cuando necesitan
varias filas. La espera es limitada; si no puede obtenerse el bloqueo, se devuelve
un error controlado y se revierte la transacción. No se realizan envíos de email
ni otras comunicaciones externas manteniendo esos bloqueos.

### Renovación y revocación de autenticación

Las operaciones que crean, consumen o revocan RefreshTokens bloquean primero
la fila de su User dentro de la transacción y vuelven a comprobar su estado.
El cambio de contraseña utiliza ese mismo bloqueo y revoca todos los grupos
del usuario junto con la modificación de passwordHash.

Consumir un refresh marca usedAt y crea otro registro del mismo grupo con
el mismo expiresAt, sin ampliar sus 7 días iniciales. Un token consumido
presentado otra vez para renovar revoca su grupo. La revocación se confirma
antes de devolver el rechazo y no se revierte por generar la respuesta de error.

La coordinación por User evita carreras entre login, renovación, logout y
cambio de contraseña. Estas reglas no modifican la capacidad de licencias.

## Generación y entrega del token de enrollment

El backend genera 32 bytes mediante `SecureRandom` y los codifica como Base64
URL-safe. La caducidad inicial es de 24 horas, configurable. Para localizar el
enrollment se calcula SHA-256 del token recibido y se consulta su hash único.

El token original solo se utiliza para entregarlo por email y para su consumo;
no se guarda en PostgreSQL ni se registra en logs. SHA-256 se utiliza para este
secreto aleatorio; las contraseñas de usuario siguen utilizando BCrypt.

Primero se confirma la reserva en PostgreSQL y después se envía el email.
Si el envío falla y el token no se ha utilizado, se revoca en otra transacción
siguiendo el bloqueo de License. Si el proceso se interrumpe entre la reserva
y el envío, el enrollment puede revocarse y, en todo caso, deja de ocupar un
puesto al caducar. No se promete entrega atómica entre PostgreSQL y el email.

## Convenciones de persistencia

- Identificadores `Long` generados mediante `IDENTITY`.
- Tablas y columnas en `snake_case`; User utiliza la tabla `users`.
- Fechas `Instant` representadas como `timestamp with time zone` en PostgreSQL.
- `eventUuid` y `familyId` representados con el tipo PostgreSQL `uuid`.
- Enums almacenados como texto con restricciones `CHECK` de valores permitidos.
- Relaciones cargadas de forma diferida cuando corresponda. No se añaden
  colecciones inversas a todas las entidades; se mantienen las necesarias,
  como las reglas de Policy.
- Fechas de auditoría de creación y modificación mediante Spring Data JPA;
  los cambios en reglas actualizan expresamente `Policy.updatedAt`.
- Esquema gestionado mediante migraciones Flyway y validado por Hibernate.

Estas convenciones preparan la futura persistencia; esta tarea no crea clases
JPA, migraciones ni configuración ejecutable del backend.

## Usuarios y administración

- Cada Company tiene uno o muchos Users y se crea con su primer ADMIN.
- Cada Company debe conservar al menos un User con el rol ADMIN.
- Se impide eliminar o cambiar a USER el rol del último ADMIN de una Company.
- El inicio de sesión utiliza email y contraseña.
- Las contraseñas se almacenan mediante BCrypt.
- Cada ADMIN administra únicamente su propia Company.

La autenticación utiliza JWT de duración máxima inicial de 15 minutos y
refresh tokens de 7 días, con duraciones configurables. La renovación automática
conserva la caducidad original y permite seguir trabajando sin repetir el login
cada vez que caduca el JWT. Logout revoca el grupo de renovación; cambiar la
contraseña revoca todos los grupos del usuario. Los JWT ya emitidos pueden
seguir válidos hasta caducar. El detalle se recoge en la
[autenticación JWT](jwt-authentication.md).

## Licencias y enrollment

- Los enrollments pendientes reservan puestos de la License.
- Los dispositivos desactivados liberan puestos.
- El backend genera el EnrollmentToken y lo envía por email.
- El token autoriza una única instalación, tiene caducidad y puede revocarse
  mientras no se haya utilizado. Reconocer un reintento no es un nuevo consumo.
- Se almacena de forma segura, sin guardar el token reutilizable en claro.

## Dispositivos

- Cada Device representa una instalación de la aplicación.
- La aplicación genera su identificador estable.
- Cada Device debe tener una Policy asignada.
- La política inicial procede del EnrollmentToken; el ADMIN puede reasignarla
  posteriormente dentro de la misma Company.

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

## Seguimiento por tareas

La tarea #4 define las cardinalidades, claves foráneas, campos obligatorios,
unicidad, longitudes, integridad entre compañías, eliminación y garantías
transaccionales de este documento. El diagrama debe reflejar también la política
inicial del enrollment y la relación con su Device de origen antes de cerrar
la revisión documental de esa tarea. La #6 añade RefreshToken, su relación
con User y las reglas técnicas de renovación; el diagrama debe recoger
esa asociación antes de cerrar su revisión.

Las siguientes decisiones se documentan en sus propias tareas, ramas y PR:

- #5: organización interna del backend Spring Boot.
- #6: autenticación JWT.
- #7: autorización por Company.
- #8: contratos iniciales de la API REST.

La aprobación conjunta del diseño no sustituye el trabajo ni las comprobaciones
de cada tarjeta. La implementación comienza con la estructura mínima del proyecto
y avanza clase a clase, sin implementar varias capas a la vez.
