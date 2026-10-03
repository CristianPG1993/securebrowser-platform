# ADR-0006: Integridad del dominio y garantías del enrollment

- Estado: Aceptada
- Fecha: 2026-10-03

## Contexto

La tarea #4 completa las cardinalidades y restricciones del modelo antes de
implementar JPA/PostgreSQL. El MVP está orientado a empresas: todo User pertenece
a una Company y las políticas son corporativas, administradas por sus ADMIN.

El diseño debe impedir asociaciones entre compañías, mantener al menos un ADMIN,
respetar la capacidad de las licencias y reconocer un registro cuyo resultado
no llegó a Desktop. También debe conservar el historial de instalaciones y
eventos generados offline.

El ADR-0005 establece los atributos y la representación básica. Este ADR concreta
su integridad y añade dos asociaciones aprobadas: EnrollmentToken.policy y
Device.enrollmentToken. Los detalles de campos y longitudes se mantienen en el
modelo de dominio.

## Opciones consideradas

| Aspecto | Opción elegida | Alternativa y compromiso |
| --- | --- | --- |
| Política inicial | El ADMIN la selecciona al emitir el enrollment. | Dejar que Desktop la elija no garantiza la configuración asignada por el ADMIN. |
| Reintentos de registro | Conservar un enrollment de origen único en Device. | Rechazar siempre un token utilizado impide recuperar directamente el resultado cuando se pierde la respuesta. |
| Integridad entre compañías | Validación en servicios transaccionales y pertenencias inmutables. | Claves compuestas o triggers reforzarían la igualdad en PostgreSQL, con más complejidad de esquema. |
| Capacidad concurrente | Calcular ocupación y bloquear la fila de License. | Un contador exige mantener otro estado; contar sin coordinar las escrituras permite superar el límite. |
| Historial | Restringir borrados y desactivar Devices. | Borrar en cascada los recursos principales elimina el contexto de enrollments y eventos. |
| Hash del enrollment | SHA-256 de un secreto aleatorio de 32 bytes. | BCrypt se mantiene para contraseñas humanas; su hash con sal no ofrece el mismo mecanismo directo de búsqueda del token. |

## Decisión

### Relaciones y pertenencia

Se mantienen las cardinalidades corporativas del modelo. Policy se relaciona
además con cero o muchos EnrollmentTokens; cada token tiene una política inicial.
EnrollmentToken puede crear cero o un Device, y cada Device conserva exactamente
un enrollment de origen mediante una clave foránea obligatoria y única.

La política actual de Device puede cambiar dentro de su Company. La política
de origen del enrollment se conserva. Company, User, License y las relaciones
de origen de Device no se reasignan; las reglas permanecen en su Policy y los
eventos en su Device.

Los servicios validan la igualdad de Company entre entidades relacionadas.
PostgreSQL garantiza existencia de referencias, obligatoriedad, unicidad y
restricciones de cada fila. Las claves foráneas simples no garantizan por sí
solas la igualdad de Company entre varias entidades.

### Enrollment y licencia

El backend genera el token con SecureRandom y 32 bytes, codifica su valor como
Base64 URL-safe y guarda únicamente su SHA-256 hexadecimal de 64 caracteres,
único globalmente. La caducidad inicial es de 24 horas configurable.

El estado se deriva de createdAt, expiresAt, usedAt y revokedAt. Utilización y
revocación son excluyentes. Un token solo se consume por primera vez mientras
está pendiente y vigente y por su usuario destinatario.

Las operaciones de reserva, consumo, revocación, actividad de Device y cambio
de capacidad bloquean la fila de License en una transacción antes de consultar
y modificar la ocupación. La ocupación es la suma de Devices activos y tokens
pendientes vigentes. No se guarda un contador.

Crear el Device y asignar usedAt ocurre en la misma transacción: la reserva se
convierte en una instalación sin ocupar un segundo puesto.

Un reintento autenticado del mismo destinatario y deviceIdentifier devuelve
el Device de origen. No crea otro Device, modifica usedAt ni reactiva una
instalación desactivada. Un identificador distinto se rechaza. La expiración
posterior no impide recuperar el resultado ya confirmado.

Se confirma la reserva antes de enviar email. Un fallo de envío revoca el token
si aún no se ha utilizado, mediante otra transacción. Una interrupción del
proceso puede dejar una reserva hasta su revocación o caducidad; PostgreSQL y
el email no constituyen una única transacción. No se mantiene un bloqueo durante
el envío ni se registra el secreto en logs.

### Último ADMIN y modificaciones de Policy

Company y su primer ADMIN se crean en una transacción. Los cambios de rol y
borrados de usuarios bloquean Company y verifican que queda al menos un ADMIN.

La edición de una Policy y sus reglas es atómica y sus modificaciones se
coordinan bloqueando Policy. Los cambios de reglas actualizan Policy.updatedAt.
Los bloqueos múltiples siguen un orden común y las esperas son limitadas.

### Unicidad e historial

Se garantizan en PostgreSQL las unicidades globales de User.email normalizado,
EnrollmentToken.tokenHash, Device.deviceIdentifier y SecurityEvent.eventUuid,
además de Device.enrollment_token_id. Las reglas son únicas por política y
valor normalizado mediante (policy_id, domain) y (policy_id, extension).

El reenvío exacto de un evento conserva el registro original y receivedAt.
Un UUID reutilizado con otro Device o contenido se rechaza. No se exige que
occurredAt sea anterior a receivedAt, debido al reloj de cliente.

Los Devices se desactivan y se conservan; los enrollments se revocan o caducan
y se conservan; los eventos son inmutables. Un Device inactivo puede sincronizar
eventos pendientes sin consumir capacidad.

Company no se elimina en el MVP. User, License y Policy solo se eliminan cuando
las reglas del modelo lo permiten y no existen referencias, incluidas las
históricas. Solo las reglas se eliminan en cascada junto con su Policy.

### Persistencia posterior

Los identificadores Long utilizan IDENTITY. Tablas y columnas utilizan snake_case,
con users para User. Instant se representa como timestamp with time zone;
eventUuid utiliza uuid y los enums texto con CHECK de valores permitidos.

La auditoría utiliza Spring Data JPA, las relaciones se cargan de forma diferida
cuando corresponde y no se añaden todas las colecciones inversas. Flyway gestiona
el esquema y Hibernate lo valida. Esta decisión no crea todavía implementación.

## Consecuencias

- La política inicial queda determinada antes de entregar el enrollment.
- La pérdida de una respuesta puede resolverse sin otra instalación ni reserva.
- El cálculo de capacidad requiere consultas coordinadas y todas las operaciones
  que cambian la ocupación deben respetar el mismo bloqueo.
- La protección del último ADMIN también requiere coordinación transaccional.
- El historial limita la eliminación de recursos que ya tienen referencias.
- La integridad entre compañías depende de los servicios; no se garantiza frente
  a escrituras SQL externas que eludan esas validaciones.
- La entrega de email puede dejar temporalmente una reserva pendiente si el
  proceso se interrumpe; caducidad y revocación permiten liberarla.
- Estructura de paquetes, autenticación, autorización y contratos REST se
  documentan por separado en las tareas #5, #6, #7 y #8.

## Validación

La revisión documental comprobará las asociaciones, cardinalidades, campos
obligatorios, restricciones, enlaces y correspondencia con el diagrama.

Durante la implementación se comprobará con PostgreSQL que:

- Dos reservas simultáneas no superan la última plaza disponible.
- Consumir un token transforma la reserva en una instalación de forma atómica.
- El reintento exacto devuelve el mismo Device; uno distinto se rechaza.
- Desactivar libera capacidad y reactivar o reducir el límite la vuelve a comprobar.
- Tokens utilizados, revocados o caducados no reservan plazas.
- Dos operaciones simultáneas no eliminan ni degradan a todos los ADMIN.
- Se rechazan asociaciones de distintas compañías y cambios de pertenencia.
- Las restricciones de unicidad y fechas rechazan datos incompatibles.
- Un evento repetido conserva el original y un UUID con otro contenido se rechaza.
- La edición de reglas actualiza Policy.updatedAt y el historial bloquea borrados.

## Referencias

- [Modelo de dominio](../architecture/domain-model.md).
- [Representación de atributos](0005-backend-domain-attributes.md).
- [Persistencia y sincronización de eventos](0003-offline-security-events.md).
- [Pertenencia de eventos](0004-security-event-ownership.md).
- [Restricciones de PostgreSQL](https://www.postgresql.org/docs/current/ddl-constraints.html).
- [Bloqueos de PostgreSQL](https://www.postgresql.org/docs/current/explicit-locking.html).
- [Recomendaciones OWASP para tokens de un solo uso](https://cheatsheetseries.owasp.org/cheatsheets/Forgot_Password_Cheat_Sheet.html).
- [Auditoría de Spring Data JPA](https://docs.spring.io/spring-data/jpa/reference/auditing.html).
