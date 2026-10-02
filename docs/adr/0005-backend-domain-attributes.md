# ADR-0005: Representación de los atributos del dominio del backend

- Estado: Aceptada
- Fecha: 2026-10-02

## Contexto

Antes de implementar las entidades JPA, se han definido y aprobado los atributos
de Company, User, License, EnrollmentToken, Device, Policy y SecurityEvent.
También se han definido UrlRule y DownloadRule, que forman parte de las reglas
de una política.

El MVP debe representar identificadores, fechas y estados de forma sencilla,
soportar el enrollment con capacidad limitada y conservar eventos generados
offline. Este ADR registra las decisiones de representación ya acordadas;
el detalle de cada atributo se mantiene en el modelo de dominio.

## Opciones consideradas

| Aspecto | Opción elegida | Alternativa | Motivo y compromiso |
| --- | --- | --- | --- |
| Identificadores internos | `Long` generado por la base de datos. | `UUID` generado sin consultar la base de datos. | `Long` facilita la lectura y depuración; necesita la base de datos para asignar el identificador. |
| Fechas | `java.time.Instant`. | `java.util.Date`. | `Instant` representa instantes absolutos y es inmutable; `Date` pertenece a la API histórica y es mutable. |
| Estado de EnrollmentToken | Derivarlo de `usedAt`, `revokedAt` y `expiresAt`. | Almacenar además un enum de estado. | Las fechas registran las transiciones y permiten calcular la caducidad; un enum adicional exigiría mantener ambos datos coherentes. |
| Ocupación de License | Calcularla mediante dispositivos activos y enrollments pendientes vigentes. | Mantener un contador persistido de puestos ocupados. | El cálculo evita un contador desactualizado; requiere consultar los registros y proteger las operaciones concurrentes. |
| Configuración de Policy | Mantener los indicadores y modos directamente en Policy. | Agrupar cada funcionalidad en una clase de configuración. | Los campos directos simplifican el MVP de dos funcionalidades; nuevas funcionalidades podrían justificar una agrupación posterior. |

## Decisión

### Identificadores

Las entidades del backend y sus reglas tendrán un `id` de tipo `Long`, generado
por la base de datos. Puede ser `null` antes de persistir un registro nuevo.

Device conserva además `deviceIdentifier`, de tipo `String`, generado por Desktop
para identificar de forma estable la instalación.

SecurityEvent conserva `eventUuid`, de tipo `java.util.UUID`, generado por Desktop.
Este valor identifica el evento durante su almacenamiento local y sus reenvíos,
conforme al ADR-0003.

### Fechas y auditoría

Las fechas se representan con `java.time.Instant`.

Company, User, License, Device y Policy utilizan `createdAt` y `updatedAt`.
Ambos valores coinciden al crear el registro. En las modificaciones posteriores
se conserva `createdAt` y se actualiza `updatedAt`.

EnrollmentToken registra su creación, caducidad y, cuando corresponda, utilización
y revocación mediante `createdAt`, `expiresAt`, `usedAt` y `revokedAt`.

SecurityEvent distingue `occurredAt`, registrado por Desktop, de `receivedAt`,
asignado por el backend al registrar el evento por primera vez. Los eventos se
conservan sin editar y los reenvíos mantienen el registro original.

Los cambios en UrlRule y DownloadRule actualizan `Policy.updatedAt`.

### Enrollment y capacidad de licencia

EnrollmentToken almacena `tokenHash`. El token original se entrega al usuario;
la base de datos conserva su hash.

Un token está pendiente y vigente cuando `usedAt` y `revokedAt` son `null`
y el instante actual es anterior a `expiresAt`. Su estado se deriva de las fechas.

La ocupación de una License es la suma de sus dispositivos activos y sus
enrollments pendientes vigentes. Los tokens caducados o revocados dejan de
reservar puestos en ese cálculo.

Device utiliza `active`, inicializado a `true` al registrarse. Desactivarlo libera
un puesto; reactivarlo debe respetar la capacidad disponible. `lastSeenAt`
registra la última comunicación conocida y no libera puestos automáticamente,
porque el MVP permite funcionamiento offline.

### Políticas y reglas

Policy contiene directamente `urlFilteringEnabled`, `urlFilteringMode`,
`downloadControlEnabled` y `downloadControlMode`.

Los modos utilizan el enum compartido `FilterMode`, con los valores `DENYLIST`
y `ALLOWLIST`. Cada colección de reglas se interpreta según el modo de su
funcionalidad en Policy.

UrlRule almacena un dominio normalizado y DownloadRule una extensión normalizada
en minúsculas y sin punto. Ambas pertenecen a una Policy; su Company se obtiene
a través de esa política.

## Consecuencias

- El modelo de dominio recoge atributos obligatorios y opcionales antes de
  implementar la persistencia.
- Los identificadores de instalaciones y eventos se conservan además de los
  identificadores internos del backend.
- Las fechas permiten distinguir la generación offline de un evento de su
  recepción posterior, así como registrar el ciclo de vida de un enrollment.
- El estado del enrollment y la ocupación de la licencia se obtienen de los
  registros que los determinan.
- La configuración de Policy es sencilla para las dos funcionalidades del MVP.
- Quedan pendientes las cardinalidades restantes, restricciones de integridad,
  mapeos JPA/PostgreSQL, generación y hash de los tokens, garantías transaccionales
  de capacidad y tratamiento de reintentos. Este ADR no elige esos mecanismos.

## Validación

La revisión documental debe comprobar que los atributos del modelo de dominio
coinciden con estas decisiones y con los ADR-0003 y ADR-0004.

Durante la implementación se comprobará que:

- Los identificadores internos se asignan al persistir los registros.
- Las fechas de creación se conservan y las de modificación se actualizan.
- Un enrollment utilizado, revocado o caducado deja de reservar un puesto.
- Consumir un enrollment convierte su reserva en una instalación activa sin
  contar dos puestos.
- Las operaciones simultáneas y los reintentos respetan la capacidad de License.
- Desactivar un Device libera un puesto y reactivarlo comprueba la capacidad.
- Reenviar un SecurityEvent conserva un único registro y su `receivedAt` original.
- Los cambios en las reglas actualizan la fecha de modificación de su Policy.

## Referencias

- [Modelo de dominio](../architecture/domain-model.md).
- [Convenciones del proyecto](../architecture/conventions.md).
- [Persistencia local y sincronización de eventos](0003-offline-security-events.md).
- [Relación de eventos con dispositivos y compañías](0004-security-event-ownership.md).
