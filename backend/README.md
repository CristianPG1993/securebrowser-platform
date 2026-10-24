# Backend de SecureBrowser Platform

Backend en Java y Spring Boot con dominio JPA, migraciones Flyway V1–V10,
API de Company, Policy, consultas de Device y sincronización de SecurityEvent,
autenticación con rotación de tokens, perfil propio,
datos demo y tratamiento común de errores.

## Guía rápida de pruebas

Ejecuta esta guía desde la raíz `securebrowser-platform`, con Docker Desktop
en modo de contenedores Linux. Compose carga `.env`; el comando de pruebas
prepara automáticamente una base de datos temporal independiente.

1. **Pruebas automáticas del backend.** Con Java 25 y Docker Desktop disponibles,
   ejecuta:

   ```powershell
   .\scripts\verify-backend.ps1
   ```

   Debe terminar con `BUILD SUCCESS`, sin fallos, errores ni pruebas omitidas.
   El script crea un PostgreSQL desechable, ejecuta `mvn verify`, retira su
   contenedor y restaura las variables de tu terminal. No requiere `.env` ni
   PostgreSQL instalado en el host. El log queda en `backend/target/verify-backend.log`.

2. **Arranque del demo con Compose.** Prepara `.env` con la
   [configuración de la entrega](#entrega-con-docker-compose-bloque-5).
   Si aún no existe, copia la plantilla sin sobrescribir tu configuración:

   ```powershell
   if (-not (Test-Path -LiteralPath .env)) { Copy-Item -LiteralPath .env.example -Destination .env }
   ```

   Completa las contraseñas y la clave JWT. Mantén `SBP_HTTP_PORT=8080` o usa
   `8081` si el puerto está ocupado. Después ejecuta:

   ```powershell
   docker compose up --build --detach --wait --wait-timeout 180
   docker compose ps
   ```

   Ambos servicios deben aparecer como `healthy`.

3. **Recorrido Postman.** Arrastra el archivo
   [auth-demo.postman_collection.json](../docs/postman/auth-demo.postman_collection.json)
   a Postman para importarlo. Crea y selecciona el entorno `SBP local` con valores locales:

   | Variable | Valor |
   | --- | --- |
   | baseUrl | http://127.0.0.1:8080/api/v1; usa 8081 si ese es tu SBP_HTTP_PORT. |
   | demoAdminPassword | Contraseña del ADMIN demo. |
   | demoUserPassword | Contraseña del USER demo. |

   Usa las contraseñas originales; reiniciar no las cambia. Pulsa `Run` con las
   28 peticiones en orden y una iteración: deben pasar las 28 comprobaciones.
   Los rechazos esperados se explican en [Comprobación con Postman](#comprobación-con-postman).

## Entrega con Docker Compose: bloque 5

Para arrancar el demo solo se necesita Docker Desktop con Compose y el motor
Linux activo. La construcción utiliza el JDK 25 y el Maven Wrapper dentro de
Docker; la imagen final ejecuta el JAR con JRE 25 y usuario `sbp` sin privilegios.
El contexto de construcción admite únicamente los archivos necesarios de
`backend`, excluyendo `.env`, pruebas y archivos generados.

En `.env` deben estar definidos estos valores:

| Variable | Contenido |
| --- | --- |
| SBP_DB_PASSWORD | Contraseña de la base de datos de Compose. |
| SBP_JWT_SECRET_BASE64 | Clave aleatoria de al menos 32 bytes codificados en Base64. |
| SBP_DEMO_ADMIN_PASSWORD | Contraseña inicial de admin@demo.local. |
| SBP_DEMO_USER_PASSWORD | Contraseña inicial de user@demo.local. |
| SBP_HTTP_PORT | 8080 por defecto; 8081 para un entorno local con 8080 ocupado. |

Cada contraseña demo exige al menos 12 caracteres y como máximo 72 bytes UTF-8.
Conserva los valores de emisor, audiencia y duraciones de la plantilla. Para
generar la clave JWT en PowerShell y copiar el resultado a `.env`:

```powershell
$sbpKeyBytes = [byte[]]::new(32)
$sbpRandom = [System.Security.Cryptography.RandomNumberGenerator]::Create()
try {
    $sbpRandom.GetBytes($sbpKeyBytes)
    [Convert]::ToBase64String($sbpKeyBytes)
} finally {
    $sbpRandom.Dispose()
    [Array]::Clear($sbpKeyBytes, 0, $sbpKeyBytes.Length)
}
```

La API se publica exclusivamente en `127.0.0.1`. Dentro del contenedor,
Spring Boot escucha en `0.0.0.0:8080`; Compose conecta el puerto local elegido
con ese puerto interno. PostgreSQL no publica puerto al host. El backend usa
`jdbc:postgresql://sbp-postgres:5432/securebrowser_db`, independientemente del
`SBP_DB_URL` destinado al arranque con Maven en la plantilla.

Compose espera a que PostgreSQL esté disponible antes de arrancar el backend.
Flyway aplica V1–V10 y Hibernate valida el esquema. La comprobación de salud
del backend verifica que Tomcat acepte conexiones; Postman comprueba el flujo
HTTP completo con autenticación y acceso a la base de datos.

El perfil demo se activa explícitamente en Compose y crea la fixture una sola
vez. Los datos se guardan en el volumen `sbp-postgres-data`: `docker compose down`
retira los contenedores y conserva el volumen. Cambiar las contraseñas del
archivo no restablece usuarios ya creados. `docker compose down --volumes`
elimina los datos de esa entrega y permite empezar desde una base vacía.
Compose usa una base distinta de la instalación PostgreSQL del host.

Consulta el estado y los logs desde la raíz:

```powershell
docker compose ps
docker compose logs --tail 80 sbp-backend
docker compose logs --tail 80 sbp-postgres
```

Para detener y volver a arrancar conservando los datos, y después repetir Postman:

```powershell
docker compose down
docker compose up --detach --wait --wait-timeout 180
```

La comprobación automática de entrega utiliza Bash, Docker Compose y Node.js
con npx. Desde la raíz, en Bash o Git Bash:

```bash
bash scripts/verify-compose.sh
```

En un entorno local con 8080 ocupado:

```bash
SBP_HTTP_PORT=8081 bash scripts/verify-compose.sh
```

El script crea un proyecto Compose temporal con credenciales desechables,
sin leer el `.env` local. Construye la imagen desde código, ejecuta las 28
comprobaciones con Newman 6.2.2, recrea los contenedores conservando el volumen
y compara el contenido íntegro de las diez tablas del dominio. Después repite
Postman y retira únicamente sus recursos temporales. Los logs se guardan bajo
`backend/target/compose-check`, excluido de Git. El historial de eventos de la
primera ejecución debe seguir presente al comenzar la segunda.

## Requisitos

Para el comando de pruebas se necesitan JDK 25 y Docker Desktop. Para arrancar
el demo con Compose solo se necesita Docker Desktop. Si ejecutas Maven
directamente, debes configurar `SBP_DB_*` en la terminal: Maven no lee `.env`.
Los apartados siguientes describen también esa alternativa de desarrollo local.

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
AuthService implementa login, rotación, logout y revocación del grupo por
reutilización. Todas las modificaciones de tokens bloquean previamente User.
Las renovaciones conservan el mismo usuario y la caducidad absoluta del grupo.
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
HTTP. Permite POST `/api/v1/auth/login`, `/api/v1/auth/refresh` y
`/api/v1/auth/logout` sin JWT; GET `/api/v1/users/me` para ADMIN y USER;
y GET/PATCH `/api/v1/companies/me`, GET `/api/v1/policies` y
GET/PATCH `/api/v1/policies/{id}` para ADMIN. Permite GET `/api/v1/devices`
y GET `/api/v1/devices/{id}/policy` a ADMIN y USER; DeviceService limita el
ámbito y exige propiedad para consultar la política de instalación. También permite
GET `/api/v1/security-events` y POST `/api/v1/devices/{id}/events/batch` a ambos roles;
SecurityEventService limita las consultas y exige propiedad para recibir lotes.
Deniega las demás rutas y
métodos. Basic, formulario, logout de Spring y caché de peticiones están
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
de esta API se incorporaron en #45. AuthController publica el login con BCrypt,
la emisión y renovación de tokens y el cierre de su grupo. El formato común
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

Para consultar Company por su identificador y guardar sus cambios, CompanyService
utiliza los métodos heredados `findById` y `save`. El flujo de usuarios añade
`findByIdForUpdate` para coordinar operaciones que protegen al último ADMIN.

`CompanyService` utiliza este repositorio para consultar la compañía propia
y guardar sus cambios, aplicando las comprobaciones de permisos.

`CompanyPersistenceTest` comprueba el mapeo y la auditoría sobre PostgreSQL.
La consulta propia de bloqueo se verifica además mediante
`RepositoryLockPersistenceTest`, descrito en el apartado siguiente.

## Consultas y bloqueos para User

`UserRepository` conserva `JpaRepository<User, Long>` y añade las consultas
necesarias para los próximos servicios de usuarios:

- Detalle por id y Company, con `Optional.empty()` tanto para un usuario
  inexistente como para uno perteneciente a otra compañía.
- Páginas por Company, con filtro opcional de rol mediante una segunda consulta,
  orden fijo por id ascendente y totales limitados al mismo ámbito.
- Existencia global del email normalizado, incluida la exclusión del propio
  usuario cuando se comprueba un cambio de email. La restricción PostgreSQL
  `uq_users_email` sigue siendo la garantía final ante escrituras concurrentes.
- Recuento por Company y rol para el futuro control del último ADMIN.
- Consulta de un usuario de la Company con bloqueo de escritura.

`CompanyRepository.findByIdForUpdate` permite coordinar cambios de rol y
borrados. Cuando una operación necesita ambas filas, el servicio debe bloquear
primero Company y después User, manteniendo los bloqueos hasta el final de
su transacción y comprobando de nuevo las reglas pertinentes bajo el bloqueo.
Si necesita varios Users, debe adquirirlos en orden de id ascendente.

Ambas consultas bloqueantes utilizan `PESSIMISTIC_WRITE` y el hint
`jakarta.persistence.lock.timeout=0`. En PostgreSQL se aplica `NOWAIT`: una
fila ocupada se rechaza inmediatamente, sin esperar su liberación ni omitir
el recurso. Spring traduce ese rechazo a una excepción de bloqueo transitoria,
compatible con el tratamiento 503 existente. Las consultas requieren una
transacción de escritura; no añaden límites transaccionales de negocio al
repositorio.

`RefreshTokenRepository.findByUser_IdAndRevokedAtIsNullOrderByIdAsc` recupera
los tokens aún no revocados de todos los grupos del usuario, incluidos los
consumidos o caducados. El futuro servicio de cambio de contraseña debe bloquear
antes el User autorizado y llamar a `RefreshToken.revoke` sobre esos registros
en la misma transacción que modifica el hash. Los registros previamente
revocados conservan su fecha. Este acceso no implementa login ni renovación.

Las pruebas de estos repositorios comparten
`PostgresRepositoryTestConfiguration`, que proporciona un PostgreSQL 17 temporal
mediante Testcontainers y `@ServiceConnection`. No requieren las variables
`SBP_DB_*` ni utilizan la base local. Con Docker disponible, desde backend:

```powershell
.\mvnw.cmd "-Dtest=UserRepositoryPersistenceTest,RepositoryLockPersistenceTest,RefreshTokenRepositoryPersistenceTest" test
```

- `UserRepositoryPersistenceTest` verifica ámbito, paginación, filtro, orden,
  totales, email y recuentos mediante consultas reales.
- `RepositoryLockPersistenceTest` utiliza transacciones y conexiones distintas
  para comprobar exclusión, rechazo `NOWAIT`, liberación tras commit/rollback
  y escritura revertida respetando el orden Company → User. Sus datos se
  confirman para compartirlos entre conexiones y se eliminan al terminar.
- `RefreshTokenRepositoryPersistenceTest` comprueba la selección por usuario y
  la revocación de los registros seleccionados, conservando grupos ajenos y
  fechas anteriores. Estos tests y los de consultas revierten sus fixtures.

Los permisos, la regla del último ADMIN y el cambio completo de contraseña se
implementarán en los servicios de las tareas #59–#61. No hay todavía endpoints
REST de usuarios.

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
| DEVICE_INACTIVE | 409 | Instalación propia inactiva al consultar su política. |
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

## Integración continua del backend

El workflow [Backend CI](../.github/workflows/backend.yml) ejecuta el job
`Backend build and tests` en GitHub Actions. Se activa al abrir o actualizar
una Pull Request hacia `main` y con los pushes a `main`. También puede
iniciarse manualmente desde la pestaña Actions.
No filtra por rutas: las PR de documentación
también ejecutan la comprobación.

El entorno utiliza Ubuntu 24.04, Temurin JDK 25 y Maven 3.9.12 mediante el
Wrapper del repositorio. Las acciones de checkout y preparación de Java
están fijadas a commits concretos. La caché de Maven reutiliza dependencias;
las pruebas se ejecutan en cada comprobación.

GitHub prepara un servicio PostgreSQL 17 y espera a que `pg_isready` indique
que está disponible. El workflow proporciona las variables `SBP_DB_URL`,
`SBP_DB_USERNAME` y `SBP_DB_PASSWORD` con credenciales desechables exclusivas
de CI. No utiliza el archivo `.env` ni la base de datos del desarrollador.
`CompanyApiPersistenceTest` arranca además su propio PostgreSQL 17 mediante
Testcontainers y `@ServiceConnection`, utilizando Docker en el runner.
Los contenedores son temporales y no se conserva su base de datos.

Desde la carpeta backend, GitHub ejecuta:

```sh
./mvnw --batch-mode --no-transfer-progress verify
```

El comando compila, ejecuta todas las pruebas y genera el JAR ejecutable.
Los contextos de persistencia aplican las migraciones Flyway V1–V10 y
Hibernate valida el esquema. La configuración JWT de pruebas procede de
`src/test/resources/application-default.properties`; CI no necesita una
clave JWT de producción. No se omiten pruebas ni se ignoran sus fallos.
Un error de compilación o de pruebas hace fallar el job.

Después, desde la raíz, CI ejecuta `bash scripts/verify-compose.sh` con el
puerto 8080. Esta fase construye y arranca la entrega real de Compose, verifica
Postman antes y después de recrear los contenedores y exige conservar los datos
del volumen. Un fallo en el arranque, la persistencia o las comprobaciones
Postman también hace fallar `Backend build and tests`.

Para ejecutar la misma verificación en PowerShell, configura primero las
variables `SBP_DB_*` para tu PostgreSQL local como se explica arriba y
mantén Docker Desktop disponible. Desde la carpeta backend:

```powershell
.\mvnw.cmd --batch-mode --no-transfer-progress verify
```

El ruleset `protect-main` exige superar el check `Backend build and tests`
con origen GitHub Actions y mantener la rama de la PR actualizada con `main`
antes de fusionar. Su activación se realizó tras verificar el workflow en
la PR #56 y en el commit fusionado en `main`: ambas ejecuciones completaron
805 pruebas sin fallos, errores ni pruebas omitidas. Los enlaces a esas
ejecuciones y las reglas de revisión están en
[CONTRIBUTING.md](../CONTRIBUTING.md#validación-y-mantenimiento-del-check-obligatorio).

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

## Autenticación y datos demo: bloque 1

Las rutas usan el prefijo `/api/v1`:

| Operación | Entrada | Resultado |
| --- | --- | --- |
| POST /auth/login | email y password | 200: accessToken, tokenType Bearer, expiresIn y refreshToken. |
| POST /auth/refresh | refreshToken vigente | 200: nuevo par; el anterior queda consumido. |
| POST /auth/logout | refreshToken conocido | 204: revoca solo su grupo. Repetirlo es válido. |
| GET /users/me | Authorization: Bearer JWT | Perfil propio; roles ADMIN y USER. |

Las contraseñas se comprueban con BCrypt coste 12. La duración máxima del JWT
es 900 segundos por defecto. El grupo de login vence a los siete días originales,
configurables mediante `SBP_REFRESH_TOKEN_LIFETIME_SECONDS`. Renovar no amplía
ese plazo; el JWT también se acorta si al grupo le queda menos tiempo.

Cada refresh contiene 32 bytes aleatorios de SecureRandom representados como
Base64 URL-safe sin relleno. Solo se persiste su SHA-256 hexadecimal.
La búsqueda inicial proyecta el ID del usuario sin cargar una entidad del token;
después se bloquea User y se relee el estado. Login, refresh y logout usan el mismo
bloqueo de fila NOWAIT. Una contención produce 503 y no modifica los tokens.

Reutilizar un refresh consumido revoca todos los miembros de su grupo y responde
401 `INVALID_REFRESH_TOKEN`. Esa excepción específica permite confirmar la
revocación antes de devolver el error. Un fallo imprevisto de emisión o persistencia
revierte la operación. Logout acepta registros conocidos usados, revocados o
caducados; un secreto desconocido o de un usuario eliminado produce el mismo 401.
Cerrar la renovación no invalida inmediatamente los JWT: conservan su caducidad.

Login incorrecto responde 401 `INVALID_CREDENTIALS`, con el mismo detalle para
email desconocido y contraseña incorrecta. Los errores usan Problem Details y
no reflejan contraseñas, secretos ni hashes. Login y refresh correctos devuelven
`Cache-Control: no-store` y `Pragma: no-cache`.

### Arrancar el demo desde PowerShell

Prepara primero PostgreSQL y las variables `SBP_DB_*` y `SBP_JWT_*` como se
explica arriba. El archivo [.env.example](../.env.example) enumera las variables;
Spring y Maven no cargan automáticamente un archivo `.env`.

Introduce las dos contraseñas iniciales —mínimo 12 caracteres y máximo 72 bytes
UTF-8— en la misma terminal donde arrancarás el backend:

```powershell
$sbpDemoAdminPassword = Read-Host 'Contraseña inicial del ADMIN demo' -AsSecureString
$env:SBP_DEMO_ADMIN_PASSWORD = [System.Net.NetworkCredential]::new('', $sbpDemoAdminPassword).Password
$sbpDemoUserPassword = Read-Host 'Contraseña inicial del USER demo' -AsSecureString
$env:SBP_DEMO_USER_PASSWORD = [System.Net.NetworkCredential]::new('', $sbpDemoUserPassword).Password
.\mvnw.cmd spring-boot:run "-Dspring-boot.run.profiles=demo" "-Dspring-boot.run.arguments=--server.address=127.0.0.1 --server.port=8080"
```

La API estará en `http://127.0.0.1:8080/api/v1`. Sin el perfil `demo` no se crean
datos ni se requieren contraseñas demo. El inicializador crea, en una transacción:

- Company `SBP Demo`, ADMIN `admin@demo.local` y USER `user@demo.local`.
- License con capacidad para cinco instalaciones.
- Policy con dominio `blocked.example` y extensión `exe` en sus listas de bloqueo.
- Device activo `b7eacb45-4aba-4a5a-8c0c-633a61294af1`, propiedad del USER,
  asociado a esa licencia y política y a un EnrollmentToken ya consumido.

El UUID fijo del dispositivo permite reconocer una fixture existente sin recrearla.
Reiniciar conserva nombres, contraseñas, reglas y demás ediciones. Cambiar las
variables de contraseña no restablece usuarios ya creados. Si hay cuentas demo
sin el dispositivo que identifica una fixture completa, el arranque falla sin
sobrescribirlas ni crear una segunda fixture.

### Comprobación con Postman

Importa [auth-demo.postman_collection.json](../docs/postman/auth-demo.postman_collection.json).
Define `demoAdminPassword` y `demoUserPassword` en un entorno local de Postman
con las contraseñas iniciales que hayas elegido. `baseUrl` vale por defecto
`http://127.0.0.1:8080/api/v1`.

Ejecuta las 28 peticiones en orden y una iteración: diez de autenticación, seis
de Policy, seis de Device y seis de SecurityEvent. Todas las comprobaciones deben
pasar. Son esperados el 401 del refresh revocado, el 403 del USER al catálogo
Policy, el 404 del ADMIN al intentar consultar o enviar como instalación del
USER y el 409 al reutilizar un UUID de evento con distinto contenido.

Ejecuta la colección en orden: login y perfil ADMIN, consultas y PATCH de Policy,
login y perfil USER, consulta de Device/Policy, envío de SecurityEvent y consulta
como ADMIN, reenvío y conflicto, rechazo de su acceso al catálogo administrativo,
renovaciones, logout y rechazo del refresh revocado. Los scripts conservan los
tokens de ADMIN y USER en variables distintas. El archivo del repositorio no
incluye contraseñas ni tokens reales.

Esta colección cubre autenticación, Policy, Device y SecurityEvent. Demuestra
ADMIN login → modifica Policy → USER login → consulta Device/Policy → envía
SecurityEvent → ADMIN consulta el evento. El bloque 5 ejecuta esta misma
colección contra la entrega de Docker Compose antes y después de recrear
los contenedores conservando sus datos.

### Cierre y límites del backend MVP

El bloque 1 no implementa el CRUD administrativo de User ni el cambio de contraseña
por HTTP. Las tarjetas #59–#64 siguen pendientes y se replanifican fuera del backend
mínimo; la consulta propia de perfil es solo la parte necesaria para autenticación.

El bloque 2 implementa las consultas y la edición de Policy descritas abajo.
El bloque 3 incorpora el listado de Device y la consulta de política de instalación.
El bloque 4 incorpora la recepción y consulta de SecurityEvent descritas abajo.
El bloque 5 entrega Compose y su comprobación automática. Con su incorporación
a main tras revisión y CI en verde quedan cerrados los cinco bloques y congelado
el alcance del backend MVP. El siguiente desarrollo pasa inmediatamente a C++/CEF,
sin continuar con CRUDs adicionales ni nuevas funcionalidades backend.

### Pruebas del bloque

`AuthServicePersistenceTest` comprueba creación, rotación, caducidad absoluta,
reutilización con revocación confirmada, aislamiento entre grupos, logout,
rollback y bloqueo compartido con cambios de contraseña.
`AuthApiPersistenceTest` recorre HTTP real mediante MockMvc, los filtros Bearer,
validación estricta, respuestas de tokens y el perfil actual de ambos roles.
`DemoDataPersistenceTest` comprueba la fixture y la conservación de ediciones
al repetir el arranque; `DemoPropertiesTest` valida las contraseñas iniciales y
que el demo está desactivado sin su perfil.

## API de Policy: bloque 2

Las tres operaciones requieren JWT y rol ADMIN; el ámbito procede del usuario
actual de PostgreSQL, sin recibir una Company elegida por el cliente.

| Ruta | Resultado |
| --- | --- |
| GET /api/v1/policies?page=0&size=20 | PageResponse<PolicyResponse>, solo de su Company, orden id ascendente. |
| GET /api/v1/policies/{id} | PolicyResponse con configuración, reglas normalizadas y fechas. |
| PATCH /api/v1/policies/{id} | PolicyResponse después de confirmar la actualización. |

page debe ser no negativo y size entre 1 y 100. Se rechazan parámetros
desconocidos, repetidos y sort libre. Un USER recibe 403; para ADMIN,
una política inexistente y una de otra Company producen el mismo 404.
JSON, IDs y datos inválidos producen 400 VALIDATION_ERROR; una contención
del bloqueo produce 503 SERVICE_UNAVAILABLE.

PATCH admite los siete campos de PolicyUpdateRequest del
[contrato REST](../docs/architecture/rest-api-contracts.md#endpoints-de-políticas).
Un campo omitido conserva su valor; null y el objeto vacío se rechazan.
Cada lista presente sustituye la anterior y [] la vacía. Se rechazan tipos
incorrectos y duplicados tras normalizar. Por ejemplo:

```json
{
  "name": "Política revisada",
  "urlRules": ["blocked.example", "phishing.example"],
  "downloadRules": [".EXE", "bat"]
}
```

PolicyService comprueba rol y pertenencia también en invocaciones directas.
Las consultas construyen el DTO dentro de una transacción REPEATABLE_READ
para leer configuración, reglas y fechas del mismo snapshot. El PATCH usa
el bloqueo de escritura NOWAIT de Policy, valida todos los valores y confirma
configuración, reglas y auditoría juntos. Las reglas comunes se conservan para
evitar conflictos de unicidad al reemplazar listas parcialmente coincidentes.
Un error imprevisto revierte la operación completa.

La colección Postman selecciona una política del listado propio, la consulta,
modifica su nombre y reglas, rechaza un PATCH con extensiones duplicadas y
comprueba que el estado confirmado permanece intacto. También verifica el
403 del USER al catálogo administrativo. Esta prueba modifica la política demo
y puede repetirse. En el bloque 3 el usuario consulta esa misma política mediante su Device.

PolicyUpdateRequestTest y PolicyServiceTest verifican lectura estricta y
permisos directos. PolicyRepositoryPersistenceTest y PolicyApiPersistenceTest
comprueban PostgreSQL real, paginación, aislamiento, confirmación, rollback
después de flush, bloqueo y lecturas coherentes mientras otra transacción edita.
POST, DELETE, asignaciones de Device y endpoints independientes de reglas
quedan fuera de este bloque.

## Consultas de Device: bloque 3

| Ruta | Permiso y resultado |
| --- | --- |
| GET /api/v1/devices | ADMIN: su Company; USER: propios. PageResponse<DeviceResponse>. |
| GET /api/v1/devices/{id}/policy | Propietario ADMIN o USER, misma Company y Device activo. PolicyResponse. |

El listado admite page (inicial 0), size (inicial 20, rango 1–100), userId
y active (true o false). Ordena por id ascendente; sus totales conservan
el mismo ámbito y filtros. USER no puede consultar otro propietario mediante
userId, aunque pertenezca a su Company. Una referencia ajena o inexistente
produce 404; los parámetros inválidos, repetidos o desconocidos producen 400.
Las consultas administrativas no modifican lastSeenAt.

La política como instalación exige ser propietario incluso con rol ADMIN;
la administración de políticas se realiza mediante /policies. Se comprueba
propiedad antes de actividad: una instalación ajena conserva 404 y una propia
inactiva responde 409 DEVICE_INACTIVE, sin registrar comunicación.

Una consulta correcta obtiene un snapshot coherente de Policy y sus reglas,
registra lastSeenAt con el Clock del backend y confirma la auditoría de Device
en la misma transacción. Se bloquea el Device con NOWAIT: la contención produce
503 y permite reintentar. REPEATABLE_READ evita mezclar partes de ediciones
concurrentes de Policy. Un fallo revierte también lastSeenAt y updatedAt.
No se modifican la política, el propietario, la licencia ni el enrollment.

Postman comprueba ADMIN lista Device → ADMIN no suplanta al propietario →
USER lista sus instalaciones → obtiene la Policy modificada por ADMIN →
verifica lastSeenAt y filtros. Importa de nuevo la colección actualizada antes
de ejecutar sus 28 comprobaciones; conserva las contraseñas en tu entorno local.
Si el backend ya estaba arrancado con una versión anterior, reinícialo con el
perfil demo para cargar las rutas nuevas.

DeviceRepositoryPersistenceTest comprueba filtros, páginas y ámbito en PostgreSQL.
DeviceServiceTest verifica los controles en invocaciones directas.
DeviceApiPersistenceTest recorre las rutas con filtros y servicios reales,
incluidos rechazos sin cambios, rollback después de flush, contención y lectura
coherente durante una edición administrativa concurrente.

GET de detalle independiente, PATCH, POST y DELETE de Device, enrollments
y administración de licencias quedan fuera de este bloque.

## Eventos de seguridad: bloque 4

| Ruta | Permiso y resultado |
| --- | --- |
| POST /api/v1/devices/{id}/events/batch | Propietario ADMIN o USER de la misma Company; Device activo o inactivo. SecurityEventBatchResponse. |
| GET /api/v1/security-events | ADMIN: Devices de su Company; USER: propios. PageResponse<SecurityEventResponse>. |

El lote contiene de 1 a 100 eventos con eventUuid, type y occurredAt UTC;
details es opcional y nullable, con máximo de 2.000 caracteres Unicode.
Desktop genera el UUID al crear el evento y conserva el mismo al reenviarlo.
Los tipos son URL_BLOCKED, DOWNLOAD_BLOCKED y POLICY_UPDATED; este último
lo comunica Desktop cuando aplica la política, no el PATCH administrativo.
Device procede de la ruta autorizada y receivedAt del Clock del backend.
Se admite un reloj Desktop adelantado y se conserva la precisión de microsegundos
del dominio, sin recortar ni normalizar details.

Un reenvío idéntico conserva el evento original y su primer receivedAt.
Los duplicados idénticos dentro del lote se reconocen una vez, en orden de
primera aparición. Reutilizar un UUID con distinto Device, tipo, details u
occurredAt produce 409 EVENT_UUID_CONFLICT sin revelar el registro existente.
Cualquier error revierte los eventos nuevos y los cambios de actividad del lote.
Solo se devuelve acceptedEventUuids después de confirmar la transacción.

La recepción bloquea el Device propio con NOWAIT y usa la unicidad global
existente mediante INSERT ... ON CONFLICT DO NOTHING. READ_COMMITTED permite
comparar con el ganador de una inserción concurrente cuando termina su commit.
Los UUID se insertan en orden estable para evitar esperas cruzadas entre lotes,
con un límite transaccional de diez segundos. La contención temporal produce
503 SERVICE_UNAVAILABLE; Desktop conserva su cola y puede reenviar el lote.
Un envío confirmado registra lastSeenAt y la auditoría en la misma transacción,
sin reactivar el Device ni modificar sus referencias.

El historial admite page (inicial 0), size (inicial 20, rango 1–100), deviceId,
type, from y to. Las fechas deben ser UTC; from incluye su extremo y to lo
excluye sobre occurredAt, con from anterior a to si aparecen ambos. Ordena
por occurredAt descendente y después id descendente. REPEATABLE_READ mantiene
coherentes la página y su total. El filtro deviceId se resuelve en el ámbito
del actor; un Device ajeno y uno inexistente producen el mismo 404.
Los listados no registran actividad. Entradas inválidas, campos desconocidos,
tipos incorrectos y parámetros repetidos o desconocidos producen 400.

Postman envía un evento con el token USER, lo consulta como ADMIN, reenvía el
mismo UUID, comprueba el conflicto al cambiar el contenido y verifica que el
historial propio conserva el id y receivedAt iniciales. También comprueba que
ADMIN no envía eventos en nombre de la instalación de USER. Cada ejecución
genera otro UUID y puede repetirse; el historial anterior se conserva.

SecurityEventRepositoryPersistenceTest y SecurityEventApiPersistenceTest
comprueban PostgreSQL real, contrato HTTP, validación, permisos, paginación,
filtros, precisión temporal, rollback después de SQL y carreras de UUID entre
Devices de la misma Company y de compañías diferentes. No se incorporan
edición, borrado, estadísticas, alertas ni cambios de entidad o migración.
