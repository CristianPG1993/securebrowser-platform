# Backend de SecureBrowser Platform

Estructura mínima del backend en Java y Spring Boot.

## Requisitos

- JDK 25.
- Spring Boot 4.1.1, definido en pom.xml.
- Maven 3.9.12, proporcionado por Maven Wrapper.
- PostgreSQL 17.
- Docker Desktop con el motor de contenedores Linux activo para las pruebas
  de integración de la API de Company con Testcontainers.

Los comandos siguientes se ejecutan desde la carpeta backend.

## Comprobar el entorno

```powershell
.\mvnw.cmd -version
```

Debe mostrar Maven 3.9.12 y Java 25.

## Preparar PostgreSQL

En una instalación nueva, conecta como administrador:

```powershell
psql -h 127.0.0.1 -p 5432 -U postgres -d postgres
```

Dentro de `psql`, crea el usuario del backend:

```sql
CREATE ROLE sbp_app LOGIN;
```

Configura su contraseña mediante la petición interactiva:

```text
\password sbp_app
```

Crea la base de datos con ese usuario como propietario:

```sql
CREATE DATABASE securebrowser_db OWNER sbp_app;
```

Para salir de `psql`:

```text
\q
```

La conexión del backend debe exigir contraseña. Si `pg_hba.conf`
contiene reglas locales generales con `trust`, añade antes de ellas:

```text
# Autenticación con contraseña para el backend de SBP.
host    securebrowser_db    sbp_app    127.0.0.1/32    scram-sha-256
host    securebrowser_db    sbp_app    ::1/128         scram-sha-256
```

En Windows, los cambios de `pg_hba.conf` se aplican a las conexiones
nuevas al guardar el archivo.

## Configurar la conexión a PostgreSQL

La base de datos de desarrollo es `securebrowser_db`, cuyo propietario
es el usuario `sbp_app`. Este usuario debe tener permiso de login y una
contraseña configurada.

Antes de arrancar el backend, define las variables en PowerShell:

```powershell
$env:SBP_DB_URL = 'jdbc:postgresql://127.0.0.1:5432/securebrowser_db'
$env:SBP_DB_USERNAME = 'sbp_app'
```

Introduce la contraseña cuando este comando la solicite:

```powershell
$sbpDbPassword = Read-Host 'Contraseña de sbp_app' -AsSecureString
```

Después, asígnala a la variable de entorno:

```powershell
$env:SBP_DB_PASSWORD = [System.Net.NetworkCredential]::new('', $sbpDbPassword).Password
```

Arranca la aplicación desde esa misma terminal. Las variables duran
durante la sesión; deben definirse de nuevo al abrir otra terminal.

Hibernate valida el esquema y Flyway aplica las migraciones de
`src/main/resources/db/migration`:

- `V1__create_companies.sql` crea la tabla `companies`.
- `V2__create_users.sql` crea la tabla `users` y su relación con Company.
- `V3__create_licenses.sql` crea la tabla `licenses`, su relación con Company
  y la restricción de capacidad mínima.
- `V4__create_policies.sql` crea la configuración base de `policies`,
  su relación con Company y las restricciones de sus modos.
- `V5__create_url_rules.sql` crea los dominios de cada Policy, la unicidad
  por política y el borrado en cascada de sus reglas.
- `V6__create_download_rules.sql` crea las extensiones de cada Policy, la unicidad
  por política y las restricciones de formato y borrado en cascada.
- `V7__create_enrollment_tokens.sql` crea los enrollments, la unicidad global
  del hash y las restricciones de referencias y ciclo de vida.
- `V8__create_devices.sql` crea las instalaciones corporativas, su UUID único,
  la unicidad del enrollment de origen y sus referencias obligatorias.
- `V9__create_security_events.sql` crea el historial de eventos, su UUID único
  global, los tipos permitidos y la referencia protegida al Device.
- `V10__create_refresh_tokens.sql` crea los refresh tokens, la unicidad global
  del hash, las restricciones temporales y el borrado en cascada desde User.

La configuración base de Policy incluye el nombre, la compañía y los
indicadores y modos de cada funcionalidad. `Policy.urlRules` comienza vacía
y se consulta mediante una colección de solo lectura. `Policy.addUrlRule`,
`UrlRule.changeDomain` y `Policy.removeUrlRule` gestionan sus cambios y actualizan
`Policy.updatedAt`; `createdAt` se conserva. La auditoría fija el instante de
modificación al sincronizar con PostgreSQL.

Los dominios se guardan en minúsculas, sin espacios exteriores ni punto final.
Los dominios internacionales se convierten a ASCII mediante
[IDN de Java](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/net/IDN.html).
Se rechazan esquemas, rutas, puertos, comodines y formatos inválidos. El límite
es de 253 caracteres ASCII y 63 por etiqueta. El mismo dominio puede aparecer
en políticas distintas; dentro de una política no puede repetirse.

Al retirar una regla se elimina su fila sin borrar Policy. Al eliminar Policy
se eliminan sus reglas. El modo sigue estando en `Policy.urlFilteringMode`.

`Policy.downloadRules` también comienza vacía y se consulta mediante una colección
de solo lectura. `Policy.addDownloadRule`, `DownloadRule.changeExtension` y
`Policy.removeDownloadRule` gestionan sus cambios y actualizan `Policy.updatedAt`,
conservando `createdAt` y las reglas de URL.

Las extensiones se guardan en minúsculas y sin un punto inicial: `.EXE` se convierte
en `exe`. Se rechazan extensiones vacías, espacios (también exteriores y Unicode),
caracteres de control, rutas y puntos interiores como `tar.gz`. El límite es de
20 caracteres después de normalizar. La unicidad se aplica dentro de cada Policy.
El modo de las reglas procede de `Policy.downloadControlMode` y su compañía de Policy.
Al retirar una regla se elimina su fila; al borrar Policy se eliminan ambas
colecciones de reglas, conservando Company.

La migración de descargas utiliza
[escapes Unicode de PostgreSQL](https://www.postgresql.org/docs/17/sql-syntax-lexical.html#SQL-SYNTAX-STRINGS-UESCAPE)
para identificar los espacios Unicode sin depender de la configuración regional.

`EnrollmentToken` conserva la License, el User y la Policy inicial. Su constructor
recibe un hash SHA-256 de 64 caracteres hexadecimales y las fechas de emisión y
caducidad proporcionadas por el backend. El hash se guarda en minúsculas y nunca
se almacena el secreto original. Las referencias, el hash y las fechas de emisión
y caducidad no tienen métodos de modificación.

La fecha `createdAt` se asigna al emitir el enrollment y no se sustituye mediante
auditoría automática al persistirlo. Las fechas se truncan a microsegundos para
respetar la [precisión temporal de PostgreSQL](https://www.postgresql.org/docs/17/datatype-datetime.html).
La caducidad debe ser posterior a la emisión también después de ese ajuste.

`markUsed` registra un primer consumo entre `createdAt` (incluido) y `expiresAt`
(excluido). `revoke` registra una revocación desde `createdAt`, incluso después de
caducar. Un enrollment utilizado no puede revocarse, uno revocado no puede usarse
y ninguna de esas transiciones se repite ni sustituye su fecha anterior.
`isPendingAt` deriva si está pendiente y vigente en un instante recibido como
argumento; no se almacena una columna de estado ni se modifica la fila al caducar.

Las referencias históricas a User, License y Policy bloquean sus borrados aunque
el enrollment esté utilizado, revocado o caducado. La generación y entrega del
secreto, la duración de la vigencia, las pertenencias a Company, la reserva de
capacidad y el consumo transaccional e idempotente se implementarán en los servicios.

`Device` conserva su Company, User, License y EnrollmentToken de origen. Su
`deviceIdentifier` es un UUID completo, normalizado en minúsculas y único en toda
la plataforma; identifica la instalación y no sirve como credencial. Cada
enrollment puede originar como máximo un Device, incluso si este se desactiva.

El nombre es opcional, admite hasta 150 caracteres y pierde los espacios exteriores.
Un nombre vacío se guarda como `null`. Las instalaciones comienzan activas;
`deactivate` y `activate` cambian ese indicador conservando el registro y sus
referencias. `assignPolicy` cambia la política actual sin modificar la política
inicial del enrollment.

La auditoría JPA asigna `createdAt` y `updatedAt` al guardar y conserva la fecha
de creación en las modificaciones. `lastSeenAt` comienza vacío y `recordLastSeen`
recibe una fecha del backend, ajustada a microsegundos. Las claves foráneas
protegen los recursos referenciados también cuando el Device está inactivo.

El registro desde el enrollment, los reintentos, la pertenencia a Company, la
ocupación de licencias al activar o desactivar y la autenticación de las
comunicaciones se coordinarán en los servicios.

`SecurityEvent` conserva el UUID generado por Desktop, el Device de origen, el
tipo, el detalle y las fechas de ocurrencia y primera recepción. El UUID se mapea
al tipo `uuid` de PostgreSQL. La Company se obtiene a través de Device y no se
duplica en el evento. Los tipos admitidos son `URL_BLOCKED`, `DOWNLOAD_BLOCKED`
y `POLICY_UPDATED`, guardados como texto.

La entidad no ofrece métodos de modificación y sus columnas de contenido no se
actualizan mediante JPA. El detalle es opcional, admite hasta 2.000 caracteres
y conserva espacios y cadenas vacías. Ambas fechas son obligatorias y se ajustan
a microsegundos: `occurredAt` procede de Desktop y `receivedAt` se proporciona
desde el backend al primer registro, sin sustituirse mediante auditoría.
Se admite una ocurrencia posterior a la recepción por desajustes del reloj cliente.

El UUID tiene unicidad global y los eventos impiden borrar su Device de origen.
Desactivar o cambiar la política del Device conserva su historial; un Device
inactivo también puede registrar eventos pendientes sin reactivarse.
La recepción autenticada, la deduplicación concurrente, la comparación de
reenvíos y la respuesta de conflicto se implementarán en los servicios de eventos.

`RefreshToken`, en el paquete `auth`, conserva User, el hash SHA-256 del secreto,
el UUID `familyId` y las fechas de creación y caducidad. El constructor recibe
estos datos del backend; valida el hash hexadecimal de 64 caracteres y lo guarda
en minúsculas. La Company se obtiene de User, sin relación obligatoria con Device
ni reserva de puestos de licencia.

Las fechas obligatorias se ajustan a microsegundos y la caducidad debe ser
posterior a la creación. No se utiliza auditoría que sustituya esas fechas.
`usedAt` y `revokedAt` comienzan en `null`. `markUsed` registra un primer consumo
entre `createdAt` (incluido) y `expiresAt` (excluido), siempre que no haya consumo
ni revocación anteriores. `revoke` registra una primera revocación desde la
creación, incluso después del consumo o de la caducidad.

En RefreshToken pueden coexistir consumo y revocación; EnrollmentToken mantiene
su regla de exclusión. Repetir cualquiera de esas transiciones se rechaza sin
sustituir su fecha. `isUsableAt` deriva la disponibilidad en un instante recibido,
sin almacenar una columna de estado ni modificar el registro al caducar.

El hash tiene unicidad global, mientras que varios registros pueden compartir
`familyId`. El borrado permitido de User elimina sus refresh tokens en cascada.
La generación del secreto, login, rotación, revocación de grupos y bloqueo de
User se implementarán en los servicios. También les corresponde conservar el
mismo usuario y la caducidad absoluta del grupo durante las renovaciones.
La duración inicial de 7 días es configurable y no se fija en la entidad.

## Compilar

```powershell
.\mvnw.cmd compile
```

## Validación JWT y usuario autenticado

Los [diagramas del flujo del backend](../docs/architecture/backend-request-flow.md)
separan el arranque de la aplicación del recorrido de cada petición y señalan
la integración del flujo de Company.

Las peticiones se autentican mediante `Authorization: Bearer <JWT>`.
El backend acepta únicamente HS256 y exige `sub`, `iss`, `aud`, `iat`, `exp`
y `jti`, con sus tipos originales correctos. `sub` identifica un User mediante
un Long positivo. El emisor y la audiencia deben coincidir con la configuración;
la emisión no puede estar en el futuro y la caducidad debe ser posterior a ella.
El token se rechaza exactamente al alcanzar `exp`, sin tolerancia adicional.
Si existe `nbf`, se respeta también su fecha de inicio.

Antes de arrancar la aplicación, configura estas variables en la misma terminal
que las de PostgreSQL:

```powershell
$env:SBP_JWT_ISSUER = 'securebrowser-backend'
$env:SBP_JWT_AUDIENCE = 'securebrowser-api'
$env:SBP_JWT_MAX_LIFETIME_SECONDS = '900'
```

La clave debe proceder de al menos 32 bytes aleatorios y expresarse en Base64.
Para generar una clave local sin imprimirla en la terminal:

```powershell
$sbpJwtBytes = [byte[]]::new(32)
$sbpJwtRandom = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $sbpJwtRandom.GetBytes($sbpJwtBytes)
    $env:SBP_JWT_SECRET_BASE64 = [Convert]::ToBase64String($sbpJwtBytes)
} finally {
    $sbpJwtRandom.Dispose()
    [Array]::Clear($sbpJwtBytes, 0, $sbpJwtBytes.Length)
}
```

Estas variables duran durante la sesión de terminal. La aplicación no incluye
una clave predeterminada. Conserva la misma clave entre arranques si necesitas
validar tokens anteriores; generar otra invalida sus firmas. Base64 representa
los bytes como texto, pero no cifra ni protege la clave. No la incluyas en Git.
La duración máxima tiene un valor predeterminado de 900 segundos; emisor,
audiencia y clave son obligatorios.

`JwtConfiguration` prepara el decodificador y un reloj UTC sustituible en pruebas.
`StrictJwtDecoder` verifica los tipos originales antes de las conversiones;
`NimbusJwtDecoder` comprueba la firma y `JwtClaimsValidator` valida los claims.
`AuthenticatedUserJwtConverter` usa `AuthenticatedUserResolver` y `UserRepository`
para obtener el usuario actual dentro de una transacción de solo lectura.
El principal `AuthenticatedUser` contiene únicamente userId, companyId y role.
`AuthenticatedUserAuthenticationToken` asigna ROLE_ADMIN o ROLE_USER desde esa
identidad; no conserva contraseñas ni el JWT como credenciales.

Cada petición consulta el User actual. La compañía y los permisos proceden de
PostgreSQL, aunque el token incluya claims alternativos de rol o compañía.
Un usuario eliminado recibe 401; un cambio de rol confirmado se aplica a la
petición siguiente con el mismo JWT. Las compañías son inmutables en el dominio:
no se incorpora una operación de traslado de usuarios.

`SecurityConfiguration` activa la seguridad de métodos y una cadena sin sesión
HTTP. Solo permite GET y PATCH `/api/v1/companies/me` al rol ADMIN; deniega
las demás rutas y métodos. Basic, formulario, logout y caché de peticiones están
deshabilitados. CSRF está deshabilitado porque la autenticación de esta API
utiliza el encabezado Bearer y no cookies. El filtro Bearer y el proveedor JWT
son los de Spring Security y se conectan expresamente sin abrir endpoints de
descubrimiento OAuth.

El procesamiento interno de errores de tipo `DispatcherType.ERROR` está
permitido para conservar los estados 400 y 404 al preparar sus respuestas.
Una petición normal a `/error` continúa denegada: esa excepción se aplica
al procesamiento interno del contenedor, no al acceso público a la ruta.

La ausencia o el rechazo de una autenticación válida produce 401 con
`WWW-Authenticate: Bearer realm="sbp"` y el código `AUTHENTICATION_REQUIRED`,
sin distinguir públicamente la causa interna. Un rol insuficiente produce
403 `ACCESS_DENIED`. Los fallos de disponibilidad
de PostgreSQL, los tiempos de espera y la imposibilidad de iniciar su transacción
producen 503 sin tratarse como credenciales incorrectas. Otros errores de
persistencia no se etiquetan indiscriminadamente como indisponibilidad temporal.
Estas respuestas utilizan el formato común `application/problem+json` y no
publican el mensaje interno de la excepción. SecurityAuthenticationEntryPoint
y SecurityAccessDeniedHandler delegan la escritura en ApiProblemDetailWriter.

`CompanyController` implementa GET y PATCH de la compañía propia y delega en
CompanyService utilizando el principal autenticado. Las pruebas específicas
de esta API se incorporaron en #45. Login, comprobación BCrypt,
emisión, refresh y logout siguen pendientes de sus flujos. El formato común
de errores MVC/Security se implementa en #46 mediante Problem Details.

### Pruebas de seguridad

`src/test/resources/application-default.properties` proporciona una clave conocida,
emisor y audiencia exclusivamente para pruebas sin perfil activo. No sustituye
la configuración de PostgreSQL ni forma parte del JAR ejecutable. Los contextos
aislados de configuración establecen sus propias propiedades para poder probar
también configuraciones inválidas. Si se activa otro perfil de pruebas, debe
proporcionarse su configuración JWT correspondiente.

Sin PostgreSQL, desde la carpeta backend:

```powershell
.\mvnw.cmd "-Dtest=AuthenticatedUserResolverTest,JwtPropertiesTest,JwtClaimsValidatorTest,StrictJwtDecoderTest,JwtConfigurationTest,AuthenticatedUserAuthenticationTokenTest,AuthenticatedUserJwtConverterTest,SecurityAuthenticationEntryPointTest,SecurityHttpTest" test
```

- `JwtPropertiesTest` comprueba los requisitos de la configuración y su binding.
- `JwtClaimsValidatorTest` comprueba los claims con un reloj fijo.
- `StrictJwtDecoderTest` aísla la comprobación de tipos usando un delegado simulado.
- `JwtConfigurationTest` utiliza el bean real y firmas reales para comprobar el
  conjunto, incluidos algoritmos, manipulación, tipos y caducidad.
- `AuthenticatedUserAuthenticationTokenTest` comprueba identidad, autoridades y
  cambios permitidos del estado de autenticación.
- `AuthenticatedUserResolverTest` y `AuthenticatedUserJwtConverterTest` usan
  Mockito para aislar la resolución, los permisos actuales y los fallos de acceso.
- `SecurityAuthenticationEntryPointTest` comprueba los estados y desafíos genéricos.
- `SecurityHttpTest` recorre filtros y firmas reales mediante MockMvc, con el
  resolver simulado. Comprueba 401/403/503, ausencia de sesión, rutas cerradas,
  cambios de permisos y el interceptor de seguridad de métodos.

Con PostgreSQL y las variables `SBP_DB_*`:

```powershell
.\mvnw.cmd "-Dtest=AuthenticatedUserResolverPersistenceTest,SecurityHttpPersistenceTest" test
```

`AuthenticatedUserResolverPersistenceTest` comprueba la consulta transaccional.
`SecurityHttpPersistenceTest` recorre JWT, filtros, CompanyController,
CompanyService y PostgreSQL reales, incluidos cambios de rol y borrados
confirmados entre peticiones. También simula el procesamiento interno de
errores en MockMvc para comprobar que conserva 400 y 404, y verifica que
`/error` sigue bloqueado ante peticiones normales. Ambos tests limpian únicamente
los usuarios y compañías que han creado.

`SecurityHttpTestSupport` contiene los tokens y el endpoint exclusivos de pruebas.
Ese endpoint observa el principal y prueba permisos; no consulta ni modifica
Company. `SecurityHttpTest` importa `ProbeConfiguration` para utilizarlo de forma
aislada; `SecurityHttpPersistenceTest` importa solo `ClockConfiguration` y utiliza
el Controller real. Los componentes exclusivos de prueba se excluyen del escaneo
normal de Spring Boot. El caso HTTP de 503 simula el fallo de persistencia;
no detiene PostgreSQL.

## Acceso a persistencia de Company

`CompanyRepository`, ubicado en `com.securebrowser.platform.company`,
extiende `JpaRepository<Company, Long>`. Spring Data JPA proporciona
su implementación y las operaciones heredadas de consulta y persistencia.

Para consultar Company por su identificador y guardar sus cambios bastan
los métodos heredados, como `findById` y `save`. Inicialmente no se necesitan
consultas adicionales.

`CompanyService` utiliza este repositorio para consultar la compañía propia
y guardar sus cambios, aplicando las comprobaciones de permisos.

La validación utiliza las pruebas existentes de Company.
`CompanyPersistenceTest` arranca el contexto de Spring y comprueba el mapeo
y la auditoría sobre PostgreSQL. Se añadirán pruebas específicas del repositorio
cuando existan consultas propias que aporten comportamiento adicional.

## Servicio de Company

`CompanyService` ofrece dos operaciones: `getOwnCompany` consulta la compañía
propia y `changeOwnCompanyName` modifica su nombre. Ambas requieren el rol
ADMIN y reciben un `AuthenticatedUser` de confianza. El identificador de la
compañía procede de `authenticatedUser.companyId()`; el servicio no recibe
una entidad User ni un identificador de compañía elegido por el cliente.

El principal procede de la validación JWT y de la consulta del usuario actual
en PostgreSQL. El servicio conserva la comprobación explícita del rol y lanza
`AccessDeniedException` para USER, también al invocarlo sin pasar por HTTP.

Las consultas utilizan una transacción de solo lectura. El cambio de nombre
se realiza en una transacción de escritura y utiliza las validaciones de
Company. Si la compañía no existe, se lanza `CompanyNotFoundException`, asociada
a HTTP 404 mediante `@ResponseStatus`.

`CompanyServiceTest` comprueba el comportamiento con Mockito, sin PostgreSQL.
`CompanyServicePersistenceTest` utiliza el servicio y repositorio reales para
comprobar la separación entre compañías, la confirmación del cambio de nombre,
la auditoría y los rechazos por nombre inválido o rol USER.

## API de Company

`CompanyController` expone únicamente estas operaciones:

| Método y ruta | Entrada | Respuesta | Permiso |
| --- | --- | --- | --- |
| GET `/api/v1/companies/me` | Sin cuerpo. | 200 `CompanyResponse`. | ADMIN de su propia compañía. |
| PATCH `/api/v1/companies/me` | JSON `CompanyUpdateRequest`. | 200 `CompanyResponse`. | ADMIN de su propia compañía. |

`@AuthenticationPrincipal` proporciona el `AuthenticatedUser` preparado por
Spring Security. `me` identifica la compañía de ese principal. El Controller
delega en el servicio y devuelve DTOs; no accede directamente al repositorio
ni devuelve la entidad JPA. La respuesta se construye después de que termine
la transacción del servicio, incluyendo las fechas auditadas.

Por ejemplo, un PATCH válido contiene:

```json
{"name":"Empresa B"}
```

El cuerpo requiere `Content-Type: application/json`. `@RequestBody` lo convierte
al DTO y `@Valid` comprueba su nombre obligatorio y longitud máxima.

`JsonConfiguration` personaliza el mapper de Jackson que utiliza Spring Boot:

- Rechaza campos desconocidos o del servidor, como `companyId`, `id` y fechas.
- Rechaza números y booleanos donde el DTO espera texto: `{"name":123}` no se
  convierte en un nombre válido; `{"name":"123"}` sí contiene texto.
- Rechaza contenido adicional después del objeto JSON principal.

Los errores de lectura o validación producen HTTP 400. Los cuerpos vacíos o
sin un nombre válido tampoco se admiten. La ausencia de autenticación y el
rol USER conservan los estados 401 y 403 de la seguridad.

`JsonConfigurationTest` comprueba estas reglas mediante un `@JsonTest` que
importa la configuración real, sin PostgreSQL:

```powershell
.\mvnw.cmd "-Dtest=JsonConfigurationTest" test
```

El formato común de errores se gestiona en el paquete `exception`, conforme a
#46. CompanyController no incorpora manejadores propios. Las pruebas HTTP y
de integración comprueban ahora tanto el estado como el cuerpo de Problem Details.

### Pruebas HTTP de Company

`CompanyControllerTest` utiliza `@WebMvcTest` con el Controller, la configuración
JSON y los filtros de seguridad reales. Sustituye CompanyService, JwtDecoder y
AuthenticatedUserResolver por mocks y proporciona directamente un principal
autenticado cuando el caso lo requiere. No conecta con PostgreSQL ni verifica
firmas JWT. Sus 37 casos comprueban:

- GET y PATCH correctos, campos públicos exactos, fechas UTC y normalización.
- Nombres inválidos, JSON incorrecto, tipos y campos no permitidos: 400 sin
  invocar el servicio.
- Ausencia de autenticación: 401; rol USER: 403, antes de validar el cuerpo.
- Traducción a 404 cuando el servicio simulado lanza CompanyNotFoundException.
- JSON común de errores, formatos no soportados y fallos 500/503 del servicio.

No requiere Docker ni variables de conexión:

```powershell
.\mvnw.cmd "-Dtest=CompanyControllerTest" test
```

`CompanyApiPersistenceTest` utiliza el contexto completo con MockMvc, JWT
firmados, filtros, Controller, Service, Repository, JPA y PostgreSQL reales.
Sus 14 casos comprueban la separación entre dos compañías, el cambio de nombre
confirmado, la auditoría y la conservación de los datos ante entradas inválidas,
rol USER, ausencia de autenticación, firma JWT incorrecta y formatos no soportados.
Los rechazos se comprueban con el cuerpo completo de Problem Details.

`CompanyApiTestConfiguration` declara un PostgreSQL 17 temporal mediante
Testcontainers. Spring gestiona el contenedor y `@ServiceConnection` proporciona
los datos de conexión a JPA y Flyway, que aplica las migraciones V1–V10.
El puerto se asigna automáticamente; esta clase no necesita las variables
`SBP_DB_*` ni utiliza la base de datos habitual del desarrollador.

Para ejecutarla:

1. Abrir Docker Desktop y esperar a que el motor de contenedores Linux esté activo.
2. Ejecutar desde la carpeta backend:

```powershell
.\mvnw.cmd "-Dtest=CompanyApiPersistenceTest" test
```

La primera ejecución descarga la imagen `postgres:17`. Se necesitan Docker
disponible y acceso al registro para descargarla si no está en la caché. Los
fallos de arranque del contenedor no se omiten ni se consideran pruebas pasadas.
La clave JWT conocida del test y el hash BCrypt de ejemplo son exclusivos de
pruebas y no forman parte de la configuración de producción.

Las peticiones ejecutan sus propias transacciones; el test no se envuelve en
una transacción con rollback. Los datos se preparan y confirman antes del HTTP,
y se consultan de nuevo en otras transacciones después de cada operación.
`@AfterEach` elimina únicamente las filas creadas y restaura la auditoría.
Al cerrarse el contexto de Spring se detiene y elimina el contenedor; la imagen
descargada permanece en la caché de Docker.

El 404 se verifica con el servicio simulado: el test de integración no elimina
una compañía que sigue referenciada por User para forzar ese caso, porque
PostgreSQL lo impide mediante su clave foránea.

## Respuestas comunes de error

Los errores utilizan `application/problem+json`, con `type: about:blank`,
`title`, `status`, `detail`, `instance` y `code`. La ruta `instance` no incluye
query. Los títulos describen el estado HTTP en inglés; los mensajes públicos
son controlados y no reproducen los mensajes internos de las excepciones.

| Código | Estado | Uso actual |
| --- | --- | --- |
| VALIDATION_ERROR | 400 | Entrada inválida o JSON que no se puede leer como el DTO. |
| AUTHENTICATION_REQUIRED | 401 | Autenticación ausente, rechazada o usuario eliminado. |
| ACCESS_DENIED | 403 | Permisos insuficientes. |
| RESOURCE_NOT_FOUND | 404 | Recurso inexistente o fuera del ámbito autorizado. |
| METHOD_NOT_ALLOWED | 405 | MVC recibe un método no soportado; conserva Allow. |
| NOT_ACCEPTABLE | 406 | Formato de respuesta no soportado. |
| UNSUPPORTED_MEDIA_TYPE | 415 | Tipo de cuerpo no soportado. |
| INTERNAL_ERROR | 500 | Error inesperado con explicación genérica. |
| SERVICE_UNAVAILABLE | 503 | Fallo temporal pertinente de una dependencia. |

La validación añade `errors`, una lista de objetos con únicamente `field` y
`message`. `field` identifica el campo del DTO, `body` para un cuerpo que no se
puede leer, o `request` para parámetros y errores generales de la petición.
No se devuelven valores rechazados, tokens, contraseñas, SQL ni stack traces.

Las responsabilidades se reparten así:

- `ApiErrorCode` conserva los códigos, estados y explicaciones públicas.
- `ApiProblemDetailFactory` construye el cuerpo y determina una ruta segura.
- `ApiFieldError` representa un fallo de entrada sin conservar su valor.
- `ApiExceptionHandler` traduce las excepciones de MVC y del servicio.
- `ApiProblemDetailWriter` escribe el mismo cuerpo desde los filtros.
- `SecurityAuthenticationEntryPoint` elige 401 o 503 al autenticar;
  `SecurityAccessDeniedHandler` responde 403.
- `ApiErrorController` sustituye la respuesta de `/error` para procesamientos
  internos `DispatcherType.ERROR`, usando el estado y la ruta originales.
  Las peticiones normales a `/error` siguen sometidas a la seguridad.

Solo TransientDataAccessException, DataAccessResourceFailureException y
CannotCreateTransactionException se clasifican como indisponibilidad temporal.
Los errores imprevistos de argumentos, programación o integridad mantienen
500. El 503 no incluye Retry-After porque no se conoce un plazo fiable.
El 401 conserva el desafío Bearer definido en el contrato.

La seguridad autoriza antes de MVC: los métodos y rutas que no permite la cadena
siguen produciendo 401/403. El 405 se aplica cuando MVC recibe un método que su
mapping no admite. `ApiExceptionHandlerTest` aísla MVC con un Controller de prueba
para comprobar ese caso y Allow, sin relajar los permisos de la aplicación.
También comprueba 406, 415, parámetros y categorías de excepciones. Los tests
de Company y Security mantienen los filtros reales activados.

Pruebas del mecanismo común sin PostgreSQL ni Docker:

```powershell
.\mvnw.cmd "-Dtest=ApiProblemDetailFactoryTest,ApiProblemDetailWriterTest,ApiExceptionHandlerTest,SecurityAccessDeniedHandlerTest,SecurityAuthenticationEntryPointTest,CompanyControllerTest,SecurityHttpTest" test
```

`SecurityHttpPersistenceTest` comprueba además el formato común en el flujo real
con PostgreSQL, los cambios de identidad y los procesamientos internos de error,
incluidas la conservación de Allow y la exclusión de la query original.

## DTOs de Company

Los DTOs se ubican en `com.securebrowser.platform.company.dto`
y separan los datos de la API de la entidad JPA.

`CompanyResponse` contiene únicamente `id`, `name`, `createdAt` y
`updatedAt`. Su método `from` copia estos valores desde Company.
Las fechas se representan en JSON como cadenas ISO 8601 en UTC.

`CompanyUpdateRequest` contiene únicamente `name`. Su constructor elimina
los espacios exteriores mediante `strip`. La validación exige contenido
y un máximo de 150 puntos de código Unicode mediante `@NotBlank` y
`@CodePointLength`, proporcionadas por el starter de validación.

La validación HTTP se aplica en CompanyController con `@Valid`, y la lectura
estricta de campos y tipos se configura mediante JsonConfiguration.

Las pruebas de los DTOs no necesitan PostgreSQL:

- `CompanyUpdateRequestTest` comprueba normalización, nombres obligatorios
  y límites de longitud con letras y emojis.
- `CompanyResponseJsonTest` comprueba la conversión y el JSON de salida,
  incluidos sus campos permitidos y fechas UTC.
- `CompanyUpdateRequestJsonTest` comprueba la deserialización y posterior
  validación cuando el nombre está ausente o es null.

Desde la carpeta backend se pueden ejecutar juntas:

```powershell
.\mvnw.cmd "-Dtest=CompanyUpdateRequestTest,CompanyResponseJsonTest,CompanyUpdateRequestJsonTest" test
```

## Ejecutar las pruebas

Las pruebas utilizan JUnit Jupiter y AssertJ.

- `CompanyTest` comprueba las reglas del nombre sin conectar con PostgreSQL.
- `UserTest` comprueba la normalización, validaciones y modificaciones
  del usuario sin conectar con PostgreSQL.
- `LicenseTest` comprueba la capacidad mínima, sus modificaciones y la compañía
  obligatoria sin conectar con PostgreSQL.
- `PolicyTest` comprueba el nombre, los valores iniciales y los cambios
  independientes de configuración sin conectar con PostgreSQL.
- `UrlRuleTest` comprueba la normalización de dominios, sus límites y la gestión
  de la colección de Policy sin conectar con PostgreSQL.
- `DownloadRuleTest` comprueba la normalización de extensiones, sus límites y
  la gestión de descargas sin alterar las reglas de URL.
- `EnrollmentTokenTest` comprueba el hash, las referencias obligatorias, las fechas,
  la precisión temporal, el estado derivado y las transiciones de uso y revocación.
- `DeviceTest` comprueba el UUID, el nombre opcional, las referencias obligatorias,
  los cambios de política y actividad y el registro de última comunicación.
- `SecurityEventTest` comprueba las referencias y fechas obligatorias, los tipos,
  el detalle opcional y su límite, la precisión temporal y los desajustes de reloj.
- `RefreshTokenTest` comprueba el hash y origen obligatorios, los límites temporales,
  el estado derivado, el primer consumo y la revocación de un token consumido.
- `CompanyPersistenceTest` comprueba el guardado, la auditoría de fechas
  y los nombres repetidos en PostgreSQL.
- `UserPersistenceTest` comprueba el guardado, la auditoría, la unicidad
  global del email y las restricciones de compañía y rol en PostgreSQL.
- `LicensePersistenceTest` comprueba el guardado, la auditoría y las restricciones
  de capacidad y compañía en PostgreSQL.
- `PolicyPersistenceTest` comprueba el guardado, la auditoría, los nombres repetidos
  y las restricciones de configuración y compañía en PostgreSQL.
- `UrlRulePersistenceTest` comprueba el guardado en cascada, la unicidad por Policy,
  los dominios normalizados, las claves foráneas y los cambios de auditoría
  al añadir, editar y retirar reglas en PostgreSQL.
- `DownloadRulePersistenceTest` comprueba el guardado, la unicidad, los límites
  y el formato de extensiones, la auditoría de Policy y la eliminación de reglas
  individuales o de ambas colecciones con su política.
- `EnrollmentTokenPersistenceTest` comprueba el guardado, la unicidad global del hash,
  las restricciones temporales y de exclusión, las claves foráneas y la protección
  de las referencias históricas en PostgreSQL.
- `DevicePersistenceTest` comprueba el guardado, la auditoría, las restricciones
  del UUID y nombre, la unicidad de instalación y enrollment, las claves foráneas
  y la conservación del origen al cambiar la política o desactivar el Device.
- `SecurityEventPersistenceTest` comprueba el guardado de UUID y tipos textuales,
  la unicidad global, los límites y referencias, las fechas originales y la
  conservación y recepción del historial de un Device inactivo.
- `RefreshTokenPersistenceTest` comprueba el guardado, la unicidad del hash,
  los grupos compartidos, las restricciones temporales, la coexistencia de consumo
  y revocación y el borrado en cascada desde User.

Para ejecutar todas las pruebas, Docker Desktop debe estar disponible para
CompanyApiPersistenceTest. Las pruebas de persistencia anteriores siguen
utilizando PostgreSQL externo y requieren las variables `SBP_DB_*` definidas
en la misma terminal:

```powershell
.\mvnw.cmd test
```

Para ejecutar únicamente las pruebas unitarias de una entidad:

```powershell
.\mvnw.cmd "-Dtest=CompanyTest" test
.\mvnw.cmd "-Dtest=UserTest" test
.\mvnw.cmd "-Dtest=LicenseTest" test
.\mvnw.cmd "-Dtest=PolicyTest" test
.\mvnw.cmd "-Dtest=UrlRuleTest" test
.\mvnw.cmd "-Dtest=DownloadRuleTest" test
.\mvnw.cmd "-Dtest=EnrollmentTokenTest" test
.\mvnw.cmd "-Dtest=DeviceTest" test
.\mvnw.cmd "-Dtest=SecurityEventTest" test
.\mvnw.cmd "-Dtest=RefreshTokenTest" test
```

Para ejecutar únicamente las pruebas de persistencia de una entidad:

```powershell
.\mvnw.cmd "-Dtest=CompanyPersistenceTest" test
.\mvnw.cmd "-Dtest=UserPersistenceTest" test
.\mvnw.cmd "-Dtest=LicensePersistenceTest" test
.\mvnw.cmd "-Dtest=PolicyPersistenceTest" test
.\mvnw.cmd "-Dtest=UrlRulePersistenceTest" test
.\mvnw.cmd "-Dtest=DownloadRulePersistenceTest" test
.\mvnw.cmd "-Dtest=EnrollmentTokenPersistenceTest" test
.\mvnw.cmd "-Dtest=DevicePersistenceTest" test
.\mvnw.cmd "-Dtest=SecurityEventPersistenceTest" test
.\mvnw.cmd "-Dtest=RefreshTokenPersistenceTest" test
```

Las pruebas de persistencia de las entidades revierten sus modificaciones
al finalizar cada prueba. `CompanyServicePersistenceTest` utiliza transacciones
que se confirman y elimina después los usuarios y compañías que ha creado,
restaurando también el proveedor de fechas de auditoría.
Las migraciones aplicadas por Flyway se conservan.

Algunas pruebas provocan rechazos SQL deliberados para comprobar las
restricciones. El resultado debe mostrar cero fallos y errores.

La fase `package` también ejecuta todas las pruebas antes de generar el JAR.

### Pruebas desde VS Code

El ejecutor de Java de VS Code utiliza su propia configuración.
Para proporcionar las variables de conexión, se puede configurar
`java.test.config.envFile` con la ruta absoluta de un archivo `.env` local.

El archivo `.env` y la carpeta `.vscode` están excluidos de Git.
El archivo `.env` no se carga automáticamente al ejecutar Maven desde
PowerShell; en ese caso deben definirse las variables de entorno.

## Empaquetar

El empaquetado ejecuta toda la suite: necesita Docker Desktop y PostgreSQL
externo con las variables `SBP_DB_*`, como se explica en la sección de pruebas.

```powershell
.\mvnw.cmd package
```

Genera el JAR ejecutable:

```text
target/securebrowser-backend-0.0.1-SNAPSHOT.jar
```

## Arrancar

```powershell
.\mvnw.cmd spring-boot:run
```

Spring Boot inicia Tomcat en el puerto 8080.
Para detener la aplicación, pulsa Ctrl+C.

Si el puerto está ocupado, puedes indicar otro:

```powershell
.\mvnw.cmd spring-boot:run "-Dspring-boot.run.arguments=--server.port=8081"
```

En Linux y macOS se utiliza ./mvnw en lugar de .\mvnw.cmd.

## Documentación

- [Organización del backend](../docs/architecture/backend-structure.md).
- [Modelo de dominio](../docs/architecture/domain-model.md).
- [Contratos REST](../docs/architecture/rest-api-contracts.md).
