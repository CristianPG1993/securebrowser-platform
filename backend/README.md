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

La configuración base de Policy incluye el nombre, la compañía y los
indicadores y modos de cada funcionalidad. Sus colecciones de reglas se
incorporarán con [UrlRule (#26)](https://github.com/CristianPG1993/securebrowser-platform/issues/26)
y [DownloadRule (#27)](https://github.com/CristianPG1993/securebrowser-platform/issues/27).

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
- `CompanyPersistenceTest` comprueba el guardado, la auditoría de fechas
  y los nombres repetidos en PostgreSQL.
- `UserPersistenceTest` comprueba el guardado, la auditoría, la unicidad
  global del email y las restricciones de compañía y rol en PostgreSQL.
- `LicensePersistenceTest` comprueba el guardado, la auditoría y las restricciones
  de capacidad y compañía en PostgreSQL.
- `PolicyPersistenceTest` comprueba el guardado, la auditoría, los nombres repetidos
  y las restricciones de configuración y compañía en PostgreSQL.

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
```

Para ejecutar únicamente las pruebas de persistencia de una entidad:

```powershell
.\mvnw.cmd "-Dtest=CompanyPersistenceTest" test
.\mvnw.cmd "-Dtest=UserPersistenceTest" test
.\mvnw.cmd "-Dtest=LicensePersistenceTest" test
.\mvnw.cmd "-Dtest=PolicyPersistenceTest" test
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