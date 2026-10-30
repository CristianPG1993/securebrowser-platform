# SecureBrowser Platform (SBP)

Proyecto de TFG del ciclo de Desarrollo de Aplicaciones Multiplataforma (DAM).

## Objetivo

Desarrollar una plataforma de navegación segura para empresas, con políticas
centralizadas, control de dispositivos y registro de eventos de seguridad.

## Componentes previstos

- **Desktop:** navegador en C++ con Chromium Embedded Framework (CEF).
- **Backend:** API en Java con Spring Boot y PostgreSQL.
- **Android:** aplicación de administración y monitorización en Kotlin.
- **Infraestructura:** Docker, Docker Compose y GitHub Actions.

## Alcance del MVP

Las funciones principales de seguridad serán el filtrado de URL y el control
de descargas. Se complementarán con autenticación, gestión de compañías,
usuarios, licencias, dispositivos, políticas y eventos de seguridad.

## Estado actual

El backend dispone del modelo de dominio implementado mediante entidades JPA,
migraciones Flyway V1–V10 y pruebas de dominio y persistencia sobre PostgreSQL.
El primer flujo REST de Company incluye Repository, Service, DTOs, Controller
y pruebas HTTP y de integración. La API permite consultar y modificar el nombre
de la compañía propia con rol ADMIN.

La seguridad valida JWT Bearer y obtiene la identidad, la compañía y el rol
actuales desde PostgreSQL. El tratamiento común de errores utiliza Problem
Details. La autenticación incluye login BCrypt, emisión JWT, rotación de refresh
tokens, revocación del grupo por reutilización y logout. GET `/api/v1/users/me`
devuelve el perfil propio. El perfil `demo` prepara datos reproducibles para
ADMIN y USER sin restablecer sus modificaciones al reiniciar.

El ADMIN puede listar, consultar y actualizar las políticas de su compañía.
El PATCH de Policy conserva los campos omitidos, reemplaza listas de reglas
y confirma configuración y auditoría de forma atómica.

El listado de Device limita al ADMIN a su compañía y al USER a sus instalaciones.
El propietario de una instalación activa puede obtener su política y registrar
su última comunicación con la hora del backend.

Desktop puede enviar lotes atómicos de eventos desde sus instalaciones,
incluidas las inactivas. El backend reconoce reenvíos por UUID, conserva la
primera recepción y limita las consultas de historial a la compañía o al
propietario. Postman demuestra el recorrido de autenticación, edición de Policy,
consulta de Device/Policy y envío y consulta de SecurityEvent.

La entrega local utiliza [Docker Compose](compose.yaml) para construir el backend
desde código y arrancarlo junto a PostgreSQL 17, con datos persistentes y API en
`http://127.0.0.1:8080`. El puerto local puede configurarse como 8081 si está ocupado.
La [guía rápida de pruebas](backend/README.md#guía-rápida-de-pruebas) incluye la
configuración, el arranque y las 28 comprobaciones Postman.

La entrega del bloque 5 cierra el backend MVP: su alcance queda congelado en
autenticación y datos demo, Policy, consultas de Device, SecurityEvent y entrega
reproducible. El siguiente desarrollo pasa a C++/CEF. No se incorporan CRUDs
adicionales ni nuevas funcionalidades backend; las funcionalidades aplazadas
quedan fuera del alcance implementado.

Los requisitos y comandos de ejecución están disponibles en el
[README del backend](backend/README.md).

Desktop Foundation incorpora ya un navegador CEF embebido en Qt 6 Widgets:
carga HTTPS, adapta el contenido al tamaño de la ventana, gestiona el foco
y coordina el cierre. Se ha comprobado localmente en Windows 11 x64, en
Debug y Release. Las condiciones, límites y comandos están en el
[README de Desktop](desktop/README.md). La navegación mediante controles
propios, REST, SQLite y Security Engine se incorporarán en pasos posteriores.

## Integración continua

El workflow [Backend CI](.github/workflows/backend.yml) compila el backend,
ejecuta todas sus pruebas y genera el JAR mediante Maven Wrapper. Utiliza
Java 25, PostgreSQL 17 y Docker para las pruebas con Testcontainers.
También comprueba la entrega de Compose en 8080 y el recorrido Postman antes y
después de recrear los contenedores conservando sus datos.
Se ejecuta en las Pull Requests hacia `main` y con los pushes a `main`,
incluidos los cambios de documentación.

La configuración y el comando para reproducir la comprobación están en el
[README del backend](backend/README.md#integración-continua-del-backend).
El ruleset `protect-main` exige superar el check `Backend build and tests`
con origen GitHub Actions y mantener la rama de la PR actualizada con `main`
antes de fusionar. CI se verificó en la PR #56 y después de su fusión.
Los resultados y las reglas de revisión están en
[CONTRIBUTING.md](CONTRIBUTING.md#validación-y-mantenimiento-del-check-obligatorio).

## Documentación

- [Arquitectura general](docs/architecture/overview.md).
- [Modelo de dominio del MVP](docs/architecture/domain-model.md).
- [Decisiones arquitectónicas (ADR)](docs/adr/).
- [Convenciones del proyecto](docs/architecture/conventions.md).
- [Guía de contribución y revisión de Pull Requests](CONTRIBUTING.md).

## Derechos de uso

Todos los derechos reservados sobre el código y la documentación originales
de SecureBrowser Platform.

No se concede una licencia general para utilizar, modificar, redistribuir
o incorporar este material en otros proyectos sin autorización expresa
del titular de los derechos.

Este aviso no limita los usos permitidos por la legislación aplicable
ni los permisos concedidos mediante las condiciones de servicio de GitHub.

Las dependencias y los materiales de terceros se rigen por sus propias licencias.
