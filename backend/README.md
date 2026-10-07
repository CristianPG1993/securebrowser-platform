# Backend de SecureBrowser Platform

Estructura mínima del backend en Java y Spring Boot.

## Requisitos

- JDK 25.
- Spring Boot 4.1.1, definido en pom.xml.
- Maven 3.9.12, proporcionado por Maven Wrapper.
- PostgreSQL 17.

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

## Compilar

```powershell
.\mvnw.cmd compile
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

Para ejecutar todas las pruebas, PostgreSQL debe estar disponible y las
variables `SBP_DB_*` deben estar definidas en la misma terminal:

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
```

Las modificaciones de las filas realizadas por las pruebas de persistencia
se revierten al finalizar cada prueba. Las migraciones aplicadas por Flyway
se conservan.

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
