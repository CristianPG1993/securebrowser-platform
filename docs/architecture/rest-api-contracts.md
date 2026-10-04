# Contratos iniciales de la API REST de SecureBrowser Platform

## Alcance

Este documento corresponde a la tarea #8 y define el contrato del MVP para
Desktop y Android antes de implementar controllers. La decisión se recoge
en el [ADR-0010](../adr/0010-rest-api-contracts.md).

Se utilizan el [modelo de dominio](domain-model.md), la
[autenticación JWT](jwt-authentication.md) y la
[autorización por Company](company-authorization.md) ya aprobados.
Los nombres de DTOs describen clases futuras, ubicadas en el subpaquete dto
de su funcionalidad según la [estructura del backend](backend-structure.md).
Esta tarea no genera Java, migraciones ni configuración ejecutable.

## Convenciones del contrato

- Prefijo común `/api/v1`. Las rutas de las tablas se añaden a ese prefijo.
- HTTPS, JSON con propiedades camelCase y `Content-Type: application/json`
  para cuerpos normales. Los errores utilizan `application/problem+json`.
- Los identificadores de entidades son enteros positivos compatibles con Long.
  UUID y secretos son cadenas. Enums se representan mediante su nombre exacto.
- Las fechas son cadenas ISO 8601 en UTC, por ejemplo
  `2026-10-04T08:00:00Z`, compatibles con Instant. Los campos opcionales de
  respuestas aparecen con null cuando no tienen valor; las listas vacías son [].
- No se exponen entidades JPA, passwordHash, tokenHash ni secretos en recursos
  administrativos. Solo login y refresh entregan sus tokens al cliente.
- Company procede del contexto autenticado; no se admite companyId en requests.
  Los IDs de referencias editables se validan dentro del ámbito permitido.
- Las escrituras devuelven resultados después de confirmar su transacción.
  No se presenta una escritura revertida como un éxito.

### Autenticación y autorización comunes

Todas las rutas requieren un JWT Bearer válido salvo `/auth/login`,
`/auth/refresh` y `/auth/logout`. Login verifica email y contraseña; refresh
y logout verifican el secreto presentado conforme a la #6. Los clientes no
envían un JWT caducado en Authorization a esas operaciones sin JWT requerido.

El contexto usa User.id, Company.id y role actuales de PostgreSQL. ADMIN es
administrador de su Company; USER dispone del ámbito individual de la #7.
El orden de control es autenticación, permiso de operación, pertenencia y
negocio. El rol no permitido produce 403 sin revelar existencia; un recurso
inexistente o fuera del ámbito permitido produce el mismo 404.

Los errores comunes de endpoints protegidos son 401, 403 cuando el rol no
permite la operación, 404 cuando se resuelve un recurso fuera de su ámbito,
y 503 si falla temporalmente una dependencia necesaria. Las tablas detallan
además los errores de entrada y negocio de cada operación.

### Validación y PATCH

Se rechazan con 400 JSON malformado, campos desconocidos, tipos incorrectos,
IDs no positivos, enums inválidos y valores fuera de las restricciones del
dominio. Las cadenas numéricas no sustituyen números ni cadenas de booleanos
a valores booleanos. Los parámetros de consulta también se validan.

En PATCH, cada campo presente sustituye ese valor; un campo ausente conserva
el valor anterior. No se permite un objeto vacío. Null solo vacía campos
opcionales: en los PATCH actuales, únicamente Device.name. Los demás campos
editables no admiten null. No se utiliza null para eliminar listas.

Cada lista de reglas enviada en PolicyUpdateRequest sustituye la lista completa;
[] elimina sus elementos. Una lista omitida se conserva. Las reglas se normalizan
y se rechazan duplicados tras normalización. Configuración, listas y updatedAt
se modifican en una sola transacción, con el bloqueo de Policy del dominio.
Este PATCH utiliza un DTO propio; no aplica JSON Patch ni JSON Merge Patch.

## DTOs de entrada

Todos los campos de creación son obligatorios salvo los señalados como opcionales.
Los campos de actualización son opcionales individualmente, con al menos uno
presente y respetando las reglas de PATCH.

| DTO | Campos y validación |
| --- | --- |
| LoginRequest | email: String válido, máximo 254; password: String no vacío, máximo 72 bytes UTF-8. Normaliza solo email. |
| RefreshRequest | refreshToken: String con el secreto tal como se entregó. No admite vacío ni null. |
| LogoutRequest | refreshToken: String con el mismo tratamiento de entrada que RefreshRequest. |
| ProfileUpdateRequest | name, lastName, email. No admite role ni Company. |
| PasswordChangeRequest | currentPassword: String no vacío y máximo 72 bytes UTF-8; newPassword: mínimo 12 caracteres y máximo 72 bytes UTF-8. Ninguna se normaliza. |
| CompanyUpdateRequest | name. |
| UserCreateRequest | name, lastName, email, password, role. La contraseña nueva respeta los límites de la #6. |
| UserUpdateRequest | name, lastName, email, role. No admite password, companyId ni campos del servidor. |
| LicenseCreateRequest | maxInstallations: entero entre 1 y el máximo de int Java. |
| LicenseUpdateRequest | maxInstallations, con el mismo límite. |
| PolicyCreateRequest | name. El servidor aplica los valores iniciales aprobados: funcionalidades activadas, modos DENYLIST y listas vacías. |
| PolicyUpdateRequest | name; urlFilteringEnabled y downloadControlEnabled: booleanos; urlFilteringMode y downloadControlMode: DENYLIST o ALLOWLIST; urlRules: lista de dominios String; downloadRules: lista de extensiones String. |
| EnrollmentCreateRequest | userId, licenseId, policyId: IDs positivos de recursos de la Company del ADMIN. |
| EnrollmentConsumeRequest | token: secreto no vacío; deviceIdentifier: UUID canónico de 36 caracteres; name: String opcional y nullable. No admite User, License, Policy, Company ni active alternativos. |
| DeviceUpdateRequest | name: String opcional y nullable; policyId: ID positivo; active: booleano. |
| SecurityEventBatchRequest | events: lista obligatoria de 1 a 100 SecurityEventRequest. |
| SecurityEventRequest | eventUuid: UUID; type: URL_BLOCKED, DOWNLOAD_BLOCKED o POLICY_UPDATED; occurredAt: fecha UTC; details: String opcional y nullable, máximo 2.000 caracteres. |

Los nombres de Company, User, lastName y Policy se recortan y deben conservar
contenido, con máximo 150 caracteres. Device.name tiene máximo 150; vacío tras
recortar se convierte en null. El email se recorta y pasa a minúsculas y sigue
siendo único globalmente. Login no aplica el mínimo de contraseña nueva ni
recorta espacios de password.

Los dominios tienen máximo 253 caracteres en ASCII tras normalizar; se rechazan
esquemas, rutas, puertos y comodines. Las extensiones tienen máximo 20 caracteres,
se normalizan sin punto inicial y en minúsculas; se rechazan espacios, rutas
y puntos interiores. Se mantienen todas las restricciones del modelo.

## DTOs de salida

| DTO | Campos |
| --- | --- |
| TokenResponse | accessToken: String; tokenType: Bearer; expiresIn: entero, duración efectiva en segundos, inicialmente como máximo 900; refreshToken: String. |
| CompanyResponse | id, name, createdAt, updatedAt. |
| UserResponse | id, companyId, name, lastName, email, role, createdAt, updatedAt. |
| LicenseResponse | id, companyId, maxInstallations, occupiedInstallations, availableInstallations, createdAt, updatedAt. |
| PolicyResponse | id, companyId, name, urlFilteringEnabled, urlFilteringMode, urlRules: lista de dominios normalizados; downloadControlEnabled, downloadControlMode, downloadRules: lista de extensiones normalizadas; createdAt, updatedAt. |
| EnrollmentResponse | id, userId, licenseId, policyId, status, createdAt, expiresAt, usedAt nullable, revokedAt nullable. |
| DeviceResponse | id, deviceIdentifier, name nullable, companyId, userId, licenseId, policyId, enrollmentTokenId, active, createdAt, updatedAt, lastSeenAt nullable. |
| SecurityEventResponse | id, eventUuid, deviceId, type, details nullable, occurredAt, receivedAt. |
| SecurityEventBatchResponse | acceptedEventUuids: lista de UUID únicos reconocidos, incluidos reenvíos idénticos, en el orden de su primera aparición en la petición. |
| PageResponse<T> | items: lista de T; page, size, totalElements, totalPages. |

Los IDs son números, los flags booleanos y las fechas cadenas UTC. Los campos
de reglas de PolicyResponse no exponen IDs JPA: se gestionan como listas dentro
de su política. El snapshot de una Policy incluye sus reglas y updatedAt
de forma coherente, sin mezclar partes de ediciones distintas.

occupiedInstallations y availableInstallations son valores derivados, no nuevos
atributos de License. Se calculan con un mismo instante de referencia y una
lectura coherente: Devices activos más enrollments pendientes vigentes; la
disponibilidad es maxInstallations menos ocupación.

EnrollmentResponse.status también es derivado: USED si existe usedAt, REVOKED
si existe revokedAt, EXPIRED si no está usado ni revocado y el reloj del servidor
ha alcanzado expiresAt, y PENDING en el resto de casos. No se añade otro enum
persistido ni contador. El token original no aparece en EnrollmentResponse.

## Listados, paginación y filtros

Todos los endpoints de lista devuelven PageResponse, incluso sin resultados.
page empieza en 0 y tiene valor inicial 0; size empieza en 20 y admite de 1
a 100. Una página sin resultados devuelve items: []; totalPages es 0 si no
hay recursos y en el resto de casos es el redondeo superior de totalElements/size.
Una página posterior a la última sigue devolviendo 200 con items vacío.

| Lista | Filtros opcionales adicionales | Orden fijo |
| --- | --- | --- |
| /users | role. | id ascendente. |
| /licenses | Ninguno. | id ascendente. |
| /policies | Ninguno. | id ascendente. |
| /enrollments | userId, licenseId, status. | id descendente. |
| /devices | userId, active. | id ascendente. |
| /security-events | deviceId, type, from, to. | occurredAt descendente, id descendente como desempate. |

En eventos, from es inclusivo y to exclusivo sobre occurredAt. Las fechas
deben ser UTC y, si están ambas presentes, from debe ser anterior a to.
Los filtros de estado, rol y tipo solo aceptan los valores definidos.
No se permite sort libre ni parámetros desconocidos en el contrato inicial.

Cada filtro de referencia se resuelve dentro del ámbito del solicitante: un
ADMIN solo puede filtrar por Users o Devices de su Company, y un USER por su
identidad o sus propios Devices cuando corresponda. Una referencia fuera de
ese ámbito produce 404. Las listas y sus totales nunca amplían el ámbito
autorizado. La paginación no reserva una instantánea entre peticiones distintas.

## Endpoints de autenticación

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos |
| --- | --- | --- | --- | --- |
| POST /auth/login | LoginRequest. | 200 TokenResponse. | Sin JWT; email y contraseña correctos. | 400 VALIDATION_ERROR; 401 INVALID_CREDENTIALS; 503 SERVICE_UNAVAILABLE. |
| POST /auth/refresh | RefreshRequest. | 200 TokenResponse. | Sin JWT; refresh válido del User asociado. | 400 VALIDATION_ERROR; 401 INVALID_REFRESH_TOKEN; 503 SERVICE_UNAVAILABLE. |
| POST /auth/logout | LogoutRequest. | 204 sin cuerpo. | Sin JWT; secreto que identifica el grupo del User asociado. | 400 VALIDATION_ERROR; 401 INVALID_REFRESH_TOKEN; 503 SERVICE_UNAVAILABLE. |

Login y refresh devuelven `Cache-Control: no-store` y `Pragma: no-cache`.
No se envían secretos en URL ni se devuelven valores de entrada en los errores.
Credenciales incorrectas conservan el mismo mensaje, sin confirmar si existe email.

Refresh rota el secreto y conserva la caducidad absoluta inicial de 7 días del
grupo. La reutilización de un refresh consumido confirma la revocación del grupo
antes del 401 genérico, sin revertirla al generar la respuesta de error.

Logout revoca únicamente el grupo identificado. Si el secreto identifica un
registro de ese grupo ya consumido, caducado o revocado y su User aún existe,
puede revocarse o reconocerse el cierre y devolverse 204, sin emitir tokens.
Un secreto desconocido o cuyo User ya no existe produce el mismo 401 genérico.
Repetir logout con un registro conocido no afecta a otros grupos.
Los JWT emitidos siguen sujetos a su propia caducidad máxima inicial de 15 minutos.

## Endpoints de perfil y Company

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /users/me | Sin cuerpo. | 200 UserResponse. | JWT, ADMIN o USER; identidad del contexto. | Ninguno adicional. |
| PATCH /users/me | ProfileUpdateRequest. | 200 UserResponse. | JWT, ADMIN o USER; únicamente su perfil. | 400 VALIDATION_ERROR; 409 EMAIL_ALREADY_EXISTS. |
| PUT /users/me/password | PasswordChangeRequest. | 204 sin cuerpo. | JWT, ADMIN o USER; contraseña actual correcta. | 400 VALIDATION_ERROR; 401 INVALID_CREDENTIALS. |
| GET /companies/me | Sin cuerpo. | 200 CompanyResponse. | JWT, ADMIN de su Company. | Ninguno adicional. |
| PATCH /companies/me | CompanyUpdateRequest. | 200 CompanyResponse. | JWT, ADMIN de su Company. | 400 VALIDATION_ERROR. |

Cambiar contraseña verifica currentPassword y guarda el nuevo hash junto con
la revocación de todos los grupos de renovación en la misma transacción con
bloqueo de User. Después del 204, el cliente elimina sus tokens y pide un nuevo
login; no reutiliza su refresh revocado. La respuesta no entrega otra sesión.
Un currentPassword incorrecto no cambia nada y el cliente no intenta refresh
como reacción a ese error de la operación de contraseña.

No hay POST ni DELETE de Company en la API pública. Su creación con el primer
ADMIN es una operación interna; no hay rutas para administración global.

## Endpoints de usuarios

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /users | Parámetros de lista. | 200 PageResponse<UserResponse>. | JWT, ADMIN; lista de su Company. | 400 VALIDATION_ERROR. |
| POST /users | UserCreateRequest. | 201 UserResponse y Location. | JWT, ADMIN; Company asignada desde el contexto. | 400 VALIDATION_ERROR; 409 EMAIL_ALREADY_EXISTS. |
| GET /users/{id} | ID de ruta. | 200 UserResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR. |
| PATCH /users/{id} | ID y UserUpdateRequest. | 200 UserResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 EMAIL_ALREADY_EXISTS o LAST_ADMIN_REQUIRED. |
| DELETE /users/{id} | ID de ruta. | 204 sin cuerpo. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 LAST_ADMIN_REQUIRED o RESOURCE_IN_USE. |

USER utiliza /users/me y no /users/{id}, aunque el id sea el propio. ADMIN
gestiona roles mediante el endpoint administrativo, también para sí mismo si
conserva el último ADMIN. No hay reseteo administrativo de contraseña en este
contrato. La creación de User utiliza BCrypt y nunca devuelve la contraseña.
El borrado permitido elimina RefreshTokens del User, conservando las reglas
de referencias históricas y último ADMIN.

## Endpoints de licencias

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /licenses | Parámetros de lista. | 200 PageResponse<LicenseResponse>. | JWT, ADMIN; lista de su Company. | 400 VALIDATION_ERROR. |
| POST /licenses | LicenseCreateRequest. | 201 LicenseResponse y Location. | JWT, ADMIN de su Company. | 400 VALIDATION_ERROR. |
| GET /licenses/{id} | ID de ruta. | 200 LicenseResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR. |
| PATCH /licenses/{id} | ID y LicenseUpdateRequest. | 200 LicenseResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 LICENSE_CAPACITY_EXCEEDED. |
| DELETE /licenses/{id} | ID de ruta. | 204 sin cuerpo. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 RESOURCE_IN_USE. |

Reducir maxInstallations por debajo de la ocupación actual es un conflicto.
Se mantiene el bloqueo de License; la respuesta de un GET no autoriza una
reserva futura sin repetir el control transaccional. Devices inactivos y
enrollments históricos también impiden eliminar una License referenciada.

## Endpoints de políticas

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /policies | Parámetros de lista. | 200 PageResponse<PolicyResponse>. | JWT, ADMIN; lista de su Company. | 400 VALIDATION_ERROR. |
| POST /policies | PolicyCreateRequest. | 201 PolicyResponse y Location. | JWT, ADMIN de su Company. | 400 VALIDATION_ERROR. |
| GET /policies/{id} | ID de ruta. | 200 PolicyResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR. |
| PATCH /policies/{id} | ID y PolicyUpdateRequest. | 200 PolicyResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR, incluidas reglas duplicadas tras normalizar. |
| DELETE /policies/{id} | ID de ruta. | 204 sin cuerpo. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 RESOURCE_IN_USE. |

UrlRule y DownloadRule se gestionan mediante listas dentro de Policy, sin
endpoints independientes. El borrado permitido elimina sus reglas. La edición
de Policy no cambia las asignaciones de Device: se utilizan sus PATCH para ello.
USER obtiene únicamente su política mediante el endpoint de instalación.

## Endpoints de enrollments

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /enrollments | Parámetros de lista. | 200 PageResponse<EnrollmentResponse>. | JWT, ADMIN; lista de su Company. | 400 VALIDATION_ERROR. |
| POST /enrollments | EnrollmentCreateRequest. | 201 EnrollmentResponse y Location. | JWT, ADMIN; User, License y Policy de su Company. | 400 VALIDATION_ERROR; 409 LICENSE_CAPACITY_EXCEEDED; 503 ENROLLMENT_DELIVERY_FAILED. |
| GET /enrollments/{id} | ID de ruta. | 200 EnrollmentResponse. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR. |
| POST /enrollments/{id}/revoke | ID; sin cuerpo. | 204 sin cuerpo. | JWT, ADMIN de la misma Company. | 400 VALIDATION_ERROR; 409 ENROLLMENT_ALREADY_USED. |
| POST /enrollments/consume | EnrollmentConsumeRequest. | 201 DeviceResponse y Location al crear; 200 DeviceResponse al reconocer un reintento. | JWT, ADMIN o USER, exclusivamente destinatario del token de su Company. | 400 VALIDATION_ERROR; 404 RESOURCE_NOT_FOUND; 409 ENROLLMENT_UNAVAILABLE o DEVICE_IDENTIFIER_CONFLICT. |

La emisión conserva la caducidad inicial configurable de 24 horas. Primero
confirma la reserva y luego entrega el secreto por email al usuario elegido;
201 confirma que terminó el envío, sin prometer recepción en su bandeja.
Si el envío falla se devuelve 503 ENROLLMENT_DELIVERY_FAILED y se intenta revocar
el token no usado en otra transacción, siguiendo el dominio. La metadata queda
consultable por ADMIN; el secreto no aparece en HTTP. Una interrupción puede
dejar la reserva pendiente hasta revocación o caducidad. El contrato no promete
atomicidad entre email y PostgreSQL ni deduplicación de emisiones repetidas.

Revocar un token ya revocado devuelve 204. Un token caducado no utilizado
también puede revocarse; uno usado produce 409 y conserva su historial.

Consumir resuelve el hash del secreto y verifica Company y destinatario antes
de evaluar su estado. Un token desconocido o ajeno produce el mismo 404.
Uno revocado o caducado sin consumo previo produce 409 ENROLLMENT_UNAVAILABLE.
Uno consumido con otro deviceIdentifier produce ese mismo conflicto de estado;
un identificador ocupado por otra instalación produce DEVICE_IDENTIFIER_CONFLICT.
La creación de Device y usedAt es atómica y no duplica la ocupación de licencia.

Un reintento del mismo usuario y deviceIdentifier devuelve el Device existente,
incluso después de caducar el token. No cambia su nombre, política, usedAt ni
actividad. Por ello la respuesta de un reintento puede incluir active: false.

## Endpoints de dispositivos

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /devices | Parámetros de lista. | 200 PageResponse<DeviceResponse>. | JWT; ADMIN: su Company; USER: propios. | 400 VALIDATION_ERROR. |
| GET /devices/{id} | ID de ruta. | 200 DeviceResponse. | JWT; ADMIN: misma Company; USER: propietario. | 400 VALIDATION_ERROR. |
| PATCH /devices/{id} | ID y DeviceUpdateRequest. | 200 DeviceResponse. | JWT, ADMIN de la misma Company; Policy relacionada también de esa Company. | 400 VALIDATION_ERROR; 409 LICENSE_CAPACITY_EXCEEDED. |
| GET /devices/{id}/policy | ID de ruta. | 200 PolicyResponse. | JWT, ADMIN o USER, propietario del Device de su Company y active = true. | 400 VALIDATION_ERROR; 409 DEVICE_INACTIVE. |

No hay POST directo de Device: nace al consumir un enrollment. No hay DELETE:
el ADMIN lo desactiva mediante active: false. Reactivarlo exige capacidad libre
y bloqueo de License; no se pueden editar su propietario ni licencia de origen.
Cambiar policyId no modifica la Policy inicial conservada en EnrollmentToken.

El acceso administrativo a /policies/{id} se distingue de obtener la política
como instalación: el ADMIN que no sea propietario no puede utilizar el segundo
endpoint en nombre del Device ajeno. Device.active no es un estado de User.
Las comunicaciones de instalación autenticadas actualizan lastSeenAt desde
el servidor; consultar un listado administrativo no simula actividad del Device.

## Endpoints de eventos de seguridad

| Método y ruta | Request | Respuesta correcta | Autenticación y permiso | Errores específicos, además de los comunes |
| --- | --- | --- | --- | --- |
| GET /security-events | Parámetros de lista y filtros de eventos. | 200 PageResponse<SecurityEventResponse>. | JWT; ADMIN: Devices de su Company; USER: propios. | 400 VALIDATION_ERROR. |
| POST /devices/{id}/events/batch | ID y SecurityEventBatchRequest. | 200 SecurityEventBatchResponse. | JWT, ADMIN o USER, propietario del Device de su Company, activo o inactivo. | 400 VALIDATION_ERROR; 409 EVENT_UUID_CONFLICT. |

Device procede de la ruta autorizada, no de cada evento. receivedAt lo asigna
el backend al primer registro; el cliente solo envía occurredAt. No se exige
occurredAt <= receivedAt, porque el reloj Desktop puede estar desajustado.
No hay edición ni eliminación de eventos en la API del MVP.

El lote se confirma en una única transacción. Un reenvío con el mismo eventUuid,
Device, tipo, details y occurredAt reconoce el registro existente sin modificar
receivedAt. Los UUID idénticos repetidos dentro del mismo lote se reconocen
una vez; si tienen contenido distinto, el lote se rechaza con 409.

Si cualquier evento es inválido o presenta un conflicto de UUID, no se conserva
ningún evento nuevo de ese lote. Los registros existentes antes de la petición
permanecen intactos. La unicidad global también protege recepciones simultáneas;
el manejo de la carrera conserva la misma semántica de reenvío o conflicto.
Un conflicto con otro Device no devuelve ese evento ni su propietario.

Solo después del commit se entrega acceptedEventUuids. Desktop elimina de su
cola SQLite únicamente esos UUID. Si pierde la respuesta o recibe un error,
conserva su cola; puede reenviar un lote idéntico sin generar duplicados.
La sincronización de Device inactivo no lo reactiva ni ocupa capacidad.

### Ejemplo de sincronización Desktop

Petición a `POST /api/v1/devices/17/events/batch` con el JWT del propietario:

```json
{
  "events": [
    {
      "eventUuid": "cfa7632d-6177-4a96-8d20-dc6dff2a7b8f",
      "type": "URL_BLOCKED",
      "occurredAt": "2026-10-04T08:00:00Z",
      "details": "Dominio bloqueado por la política aplicada."
    }
  ]
}
```

Respuesta 200, después de confirmar el lote o reconocer su reenvío idéntico:

```json
{
  "acceptedEventUuids": [
    "cfa7632d-6177-4a96-8d20-dc6dff2a7b8f"
  ]
}
```

## Estados HTTP y cabeceras

| Estado | Uso |
| --- | --- |
| 200 | Consulta, PATCH con recurso resultante, login, refresh, reintento de enrollment y lote reconocido. |
| 201 | Creación de User, License, Policy, Enrollment o primer Device por consumo; incluye el DTO creado y Location del detalle. |
| 204 | Borrado permitido, revocación, logout y cambio de contraseña; sin cuerpo. |
| 400 | Validación de cuerpo, ruta o query, JSON malformado o campo desconocido. |
| 401 | Autenticación o credenciales inválidas, según el flujo; no se omite la comprobación de User. |
| 403 | Rol que no permite la operación, sin revelar recursos. |
| 404 | Recurso inexistente o fuera del ámbito autorizado, incluidas referencias relacionadas. |
| 409 | Conflicto con el estado o integridad del dominio. |
| 503 | Indisponibilidad temporal, espera de bloqueo agotada o envío de enrollment fallido. |

Location utiliza rutas del contrato, por ejemplo `/api/v1/users/42` o
`/api/v1/devices/17`. Los 401 incluyen `WWW-Authenticate: Bearer realm="sbp"`,
como desafío del esquema de acceso de la API. Los clientes distinguen un JWT
rechazado de un error de credenciales o de refresh mediante code y la operación
solicitada; la cabecera no sustituye sus cuerpos de autenticación ni exige
enviar un JWT a login, refresh o logout. No se exponen causas internas ni secretos.

También se conservan los estados de protocolo: 405 si la ruta no admite el
método (con Allow), 406 si no puede producir el formato solicitado y 415 si no
admite el Content-Type. Un error inesperado produce 500 con detalle genérico;
no se transforma cualquier fallo de programación en 503.
No se incluye Retry-After si no se conoce un plazo fiable de recuperación.

## Formato común de errores

Se utiliza Problem Details conforme a RFC 9457 y el soporte ProblemDetail de
Spring. El cuerpo contiene type, title, status, detail e instance, y la extensión
code con un identificador estable de SBP. type utiliza about:blank para estos
errores de estado HTTP; title describe el estado y code distingue el caso del
dominio. instance contiene la ruta de la petición sin query ni secretos.

Los errores de validación añaden errors como lista de objetos con field y
message. No incluyen rejectedValue, contraseñas, tokens, SQL, stack traces ni
contenido de recursos ajenos. Los clientes toman decisiones con status y code,
sin analizar el texto de detail o message.

```json
{
  "type": "about:blank",
  "title": "Conflicto",
  "status": 409,
  "detail": "No hay capacidad disponible en la licencia.",
  "instance": "/api/v1/enrollments",
  "code": "LICENSE_CAPACITY_EXCEEDED"
}
```

| Estado | code | Situación |
| --- | --- | --- |
| 400 | VALIDATION_ERROR | Entrada inválida; errors identifica campos sin copiar valores sensibles. |
| 401 | AUTHENTICATION_REQUIRED | Falta JWT válido o su User ya no existe; no revela la causa interna. |
| 401 | INVALID_CREDENTIALS | Login o contraseña actual incorrectos, con respuesta genérica. |
| 401 | INVALID_REFRESH_TOKEN | Refresh rechazado o logout con secreto desconocido; reutilización confirma primero la revocación. |
| 403 | ACCESS_DENIED | El rol no permite esa operación. |
| 404 | RESOURCE_NOT_FOUND | Recurso inexistente o fuera del ámbito, sin distinguirlos. |
| 409 | EMAIL_ALREADY_EXISTS | El email normalizado no puede utilizarse; no se identifica al titular ni su Company. |
| 409 | LAST_ADMIN_REQUIRED | El cambio dejaría Company sin ADMIN. |
| 409 | RESOURCE_IN_USE | El borrado viola referencias, incluso históricas. |
| 409 | LICENSE_CAPACITY_EXCEEDED | Reserva, reactivación o reducción incompatible con la ocupación. |
| 409 | ENROLLMENT_ALREADY_USED | Intento de revocar un token utilizado. |
| 409 | ENROLLMENT_UNAVAILABLE | Consumo incompatible con estado, caducidad o instalación previa del token. |
| 409 | DEVICE_IDENTIFIER_CONFLICT | El UUID identifica otra instalación existente. |
| 409 | DEVICE_INACTIVE | La instalación propia no puede obtener nuevas políticas mientras esté inactiva. |
| 409 | EVENT_UUID_CONFLICT | UUID reutilizado con otro Device o contenido. |
| 503 | SERVICE_UNAVAILABLE | Dependencia temporalmente indisponible o bloqueo no obtenido a tiempo. |
| 503 | ENROLLMENT_DELIVERY_FAILED | Envío de email fallido después de intentar emitir el enrollment. |
| 405 | METHOD_NOT_ALLOWED | Método no disponible para la ruta. |
| 406 | NOT_ACCEPTABLE | Formato de respuesta solicitado no soportado. |
| 415 | UNSUPPORTED_MEDIA_TYPE | Tipo de cuerpo no soportado. |
| 500 | INTERNAL_ERROR | Error inesperado con respuesta genérica. |

Spring MVC, el manejador común de excepciones y los mecanismos de rechazo
de Spring Security producirán este mismo formato cuando el backend genere
la respuesta. No basta con un manejador de controllers para errores ocurridos
antes de entrar en ellos.

## Uso previsto por Desktop y Android

Desktop inicia sesión, consume su enrollment una vez y conserva el id y UUID
de Device. Obtiene su Policy mediante el endpoint de instalación, registra
eventos localmente y sincroniza lotes reconocidos por UUID al recuperar conexión.
Un 409 DEVICE_INACTIVE no elimina la política local ni eventos pendientes;
no implica que el User esté deshabilitado.

Android obtiene el perfil con /users/me después del login. Las pantallas
administrativas usan las rutas ADMIN de la misma Company; un USER solo obtiene
su perfil, Devices y eventos. El rol de la UI procede de UserResponse, mientras
el backend mantiene las comprobaciones independientemente de la interfaz.

Ambos clientes coordinan una sola renovación simultánea y guardan el nuevo par
de tokens de forma segura. Ante 401 AUTHENTICATION_REQUIRED en una comunicación
protegida pueden renovar y reintentar una vez. No renuevan por 403, 404, conflictos
o credenciales incorrectas. Un refresh rechazado pide un nuevo login.

Los lotes de eventos y el consumo idéntico de enrollment tienen los reintentos
definidos arriba. Un 503 o pérdida de respuesta no autoriza repetir automáticamente
otras creaciones, especialmente emisión de enrollments; debe comprobarse su
resultado antes de generar otra reserva. No se añade una clave general de
idempotencia para todas las operaciones del MVP.

## Casos de prueba previstos

| Caso | Resultado que se comprobará al implementar |
| --- | --- |
| Cada endpoint de las tablas | Request y response coinciden con el DTO, tipo JSON, estado y permiso documentados. |
| Serialización de respuestas | Sin entidades, hashes, contraseñas ni secretos administrativos; null y listas vacías coherentes. |
| Campos desconocidos, readonly, tipos y enums inválidos | 400 VALIDATION_ERROR y ninguna escritura. |
| PATCH ausente, null, vacío y arrays | Conserva campos ausentes, solo vacía campos opcionales y sustituye listas completas de forma atómica. |
| Login y refresh | Cuatro campos, no-store, duración efectiva y rotación; renovación sin JWT vigente. |
| Logout repetido de un grupo conocido | 204, sin afectar otros grupos ni emitir tokens. |
| Cambio de contraseña | Verifica la actual, confirma hash y revocaciones juntos, responde 204 y no entrega sesión nueva. |
| Perfil y cambio de email | Unicidad global, sin modificar rol ni Company; identidad del JWT conservada. |
| Dos Companies y varios usuarios | 403 por rol; mismo 404 para ajeno e inexistente; referencias y totales dentro del ámbito. |
| Listados vacíos y paginación | page desde 0, size 20 por defecto, máximo 100, orden documentado y totales del ámbito. |
| Filtros inválidos o de recursos ajenos | 400 o 404 según el caso, sin ampliar acceso ni revelar registros. |
| Creaciones y borrados | 201 con Location del recurso o 204 sin cuerpo, solo después de commit. |
| Último ADMIN, reducción de capacidad y recursos referenciados | 409 con code correspondiente, sin violar integridad incluso con concurrencia. |
| Policy y reglas | Normalización, duplicados rechazados, reemplazo de listas y snapshot coherente. |
| Emisión de enrollment y fallo de email | Metadata sin secreto, reserva confirmada antes de envío y tratamiento 503 con revocación o caducidad conforme al dominio. |
| Consumo y reintento del mismo enrollment | 201 inicial, 200 repetido, sin nueva ocupación ni reactivación. |
| Enrollment desconocido, ajeno, caducado o revocado | Mismos 404 para desconocido y ajeno; 409 solo después de autorizar su destinatario. |
| Policy de Device activo e inactivo | Solo propietario; 200 o 409 DEVICE_INACTIVE; consulta administrativa independiente. |
| Lote de eventos de 1, 100, 0 y 101 elementos | Límites correctos; rechazo de tamaños inválidos con 400. |
| Reenvíos y UUID repetidos dentro del lote | Reconocimiento único del mismo contenido sin cambiar receivedAt. |
| Un evento inválido o conflictivo en lote | Ningún evento nuevo confirmado; registros anteriores conservados. |
| Eventos simultáneos, offline y de Device inactivo | Deduplicación transaccional, ACK posterior al commit y ninguna reactivación. |
| Fallo de DB o bloqueo agotado | 503, sin permitir acceso ni presentar credenciales como incorrectas. |
| Errores de MVC y de Spring Security | Problem Details común; WWW-Authenticate y estados de protocolo cuando correspondan; sin datos sensibles. |

La revisión de esta tarea comprueba documentación, referencias y cobertura.
Las pruebas todavía no están implementadas ni ejecutadas. Al desarrollar se
utilizarán pruebas HTTP y de servicio, y PostgreSQL real para consultas, totales,
transacciones, bloqueos y deduplicación. Los mocks no validan esas garantías.

## Referencias

- [ADR-0010](../adr/0010-rest-api-contracts.md).
- [Modelo de dominio](domain-model.md).
- [Autenticación JWT](jwt-authentication.md).
- [Autorización por Company](company-authorization.md).
- [Organización del backend](backend-structure.md).
- [RFC 9110: semántica HTTP](https://www.rfc-editor.org/rfc/rfc9110.html).
- [RFC 9457: Problem Details](https://www.rfc-editor.org/rfc/rfc9457.html).
- [Spring MVC: respuestas de error](https://docs.spring.io/spring-framework/reference/web/webmvc/mvc-ann-rest-exceptions.html).
- [Tarea #8](https://github.com/CristianPG1993/securebrowser-platform/issues/8).
