# Organización interna del backend de SecureBrowser Platform

## Alcance

Este documento corresponde a la tarea #5 y define la ubicación y responsabilidad
del código antes de crear el proyecto Spring Boot. La decisión se recoge en el
[ADR-0007](../adr/0007-backend-package-organization.md).

El backend utiliza Java, Spring Boot, Spring Web, Spring Security, Spring Data JPA,
Hibernate y PostgreSQL. Para las pruebas se prevén JUnit 6, Mockito y MockMvc.
Los atributos y reglas del dominio están definidos en el
[modelo de dominio](domain-model.md).

Esta tarea documenta la estructura. No genera clases, configuración ejecutable,
migraciones ni contratos REST. La autenticación, autorización por Company y
contratos de API se documentan respectivamente en las tareas #6, #7 y #8.

## Organización por funcionalidad

El paquete base es `com.securebrowser.platform`. Las funcionalidades agrupan
sus entidades, repositorios, servicios y controllers. Los DTOs se sitúan en un
subpaquete `dto` de la funcionalidad correspondiente.

```text
backend/
└── src/
    ├── main/
    │   ├── java/com/securebrowser/platform/
    │   │   ├── SecureBrowserPlatformApplication.java
    │   │   ├── company/
    │   │   ├── user/
    │   │   ├── license/
    │   │   ├── enrollment/
    │   │   ├── device/
    │   │   ├── policy/
    │   │   ├── securityevent/
    │   │   ├── auth/
    │   │   ├── security/
    │   │   ├── config/
    │   │   └── exception/
    │   └── resources/
    │       └── db/migration/
    └── test/
        ├── java/com/securebrowser/platform/
        └── resources/
```

El árbol es una referencia de la estructura prevista, no una lista de archivos
que deban crearse de inmediato. Los paquetes aparecen conforme se implementa
su primera clase. La clase de arranque estará en el paquete base.

## Responsabilidad de los paquetes

| Paquete | Responsabilidad |
| --- | --- |
| `company` | Company, sus datos y las operaciones de su ciclo de vida, incluida la creación con el primer ADMIN. |
| `user` | User, UserRole, datos personales y reglas de administración de usuarios. |
| `license` | License y reglas de capacidad de instalaciones. |
| `enrollment` | EnrollmentToken, emisión, consumo, revocación y coordinación del registro de una instalación. |
| `device` | Device, política asignada, actividad y datos de la instalación. |
| `policy` | Policy, FilterMode, UrlRule y DownloadRule, con sus reglas de configuración y normalización. |
| `securityevent` | SecurityEvent, SecurityEventType, consulta, recepción y deduplicación de eventos. |
| `auth` | Entrada y coordinación de login, renovación y logout, con controller, servicio y DTOs, además de RefreshToken y su repositorio cuando se implementen. |
| `security` | Integración con Spring Security, validación del JWT y representación del usuario autenticado. |
| `config` | Configuración técnica compartida que no corresponde a una funcionalidad concreta. La configuración de Spring Security se mantiene en `security`. |
| `exception` | Traducción centralizada de excepciones a respuestas HTTP y excepciones compartidas por varias funcionalidades. |

UrlRule y DownloadRule pertenecen al paquete `policy`, porque forman parte de
la configuración de una política. Los enums se ubican junto a su dominio:
UserRole en `user`, FilterMode en `policy` y SecurityEventType en `securityevent`.

La tarea #6 añade la entidad técnica RefreshToken al paquete `auth`, conforme
al [ADR-0008](../adr/0008-jwt-authentication.md). La persistencia de los secretos
de renovación mediante sus hashes forma parte de esa funcionalidad.

Una excepción específica del negocio puede permanecer en su funcionalidad.
El paquete `exception` reúne el tratamiento HTTP y las excepciones compartidas,
sin trasladar las decisiones del dominio al manejador de errores.

## Organización dentro de una funcionalidad

Ejemplo de ubicación futura para usuarios:

```text
user/
├── User.java
├── UserRole.java
├── UserRepository.java
├── UserService.java
├── UserController.java
└── dto/
    ├── UserCreateRequest.java
    ├── UserUpdateRequest.java
    └── UserResponse.java
```

Los nombres ilustran la convención; los campos y operaciones de los DTOs y
controllers se concretan en el contrato REST de la tarea #8. No todas las
funcionalidades necesitan todas estas clases y no se crean archivos vacíos
para reproducir el ejemplo.

| Elemento | Responsabilidad y límites |
| --- | --- |
| Controller | Recibe la petición, valida su estructura, obtiene el contexto autenticado, llama al servicio y devuelve la respuesta HTTP. No calcula capacidad ni decide reglas de negocio. |
| Service | Ejecuta casos de uso, comprueba permisos y pertenencia de recursos, aplica reglas del dominio y establece los límites transaccionales. |
| Repository | Consulta y persiste entidades. Incluye las consultas de pertenencia y bloqueos necesarios, sin sustituir la coordinación transaccional del servicio. |
| Entity | Representa atributos, asociaciones y restricciones persistentes del dominio. No conoce peticiones HTTP ni constituye una respuesta de la API. |
| DTO | Representa entrada o salida de la interfaz REST. Separa el contrato público de las entidades persistentes. |

El flujo habitual es `Controller -> Service -> Repository`. El servicio puede
coordinar recursos de varias funcionalidades para completar un caso de uso,
como el registro mediante enrollment. Se evita duplicar reglas o introducir
dependencias circulares entre servicios.

Las comprobaciones de pertenencia y reglas de negocio se conservan en el servicio
aunque existan validaciones de entrada en el controller. Los casos de uso críticos
respetan las garantías transaccionales del
[ADR-0006](../adr/0006-domain-integrity-and-enrollment.md).

## Dependencias y configuración

- Las dependencias se proporcionan mediante constructores para que sean explícitas
  y puedan sustituirse en pruebas.
- Los servicios son clases concretas. No se crea una pareja de interfaz y clase
  `Impl` por cada servicio sin una necesidad real de varias implementaciones.
- Se mantiene la separación entre responsabilidades dentro de cada funcionalidad,
  sin añadir módulos Maven, microservicios, puertos y adaptadores ni capas genéricas.
- La configuración de ejecución se sitúa en `src/main/resources` y la configuración
  sensible se obtiene mediante variables de entorno, conforme a las
  [convenciones](conventions.md).
- Las migraciones Flyway se sitúan en `src/main/resources/db/migration` cuando se
  implemente el esquema. La tarea #5 no crea migraciones.

## Organización de las pruebas

Las pruebas se ubican bajo `src/test/java/com/securebrowser/platform` y reproducen
el paquete de la funcionalidad probada. Su configuración y recursos específicos
se sitúan en `src/test/resources`.

Ejemplo de nombres y ubicación futura:

```text
src/test/java/com/securebrowser/platform/
├── user/
│   ├── UserServiceTest.java
│   └── UserControllerTest.java
└── license/
    └── LicenseRepositoryIntegrationTest.java
```

| Tipo de prueba | Qué comprueba | Herramientas previstas |
| --- | --- | --- |
| Unidad de servicio | Decisiones de negocio y errores, sustituyendo dependencias externas cuando sea útil. | JUnit 6 y Mockito. |
| HTTP | Validación de entrada, respuestas, errores y permisos del endpoint según el contrato acordado. | JUnit 6 y MockMvc. |
| Integración de persistencia y servicios | Consultas, restricciones, transacciones, bloqueos y concurrencia sobre PostgreSQL real. | JUnit 6 y un entorno PostgreSQL de pruebas. |

Los mocks no verifican restricciones ni bloqueos de PostgreSQL. Las garantías de
capacidad, último ADMIN, deduplicación y reintentos requieren pruebas de integración
cuando se implementen. No se escriben pruebas de getters ni de archivos vacíos
para justificar la estructura.

## Implementación posterior

Una vez terminadas las tareas de diseño, el desarrollo empieza por la estructura
mínima del proyecto y su arranque. Las funcionalidades se incorporan clase a clase
y método a método, siguiendo el modelo aprobado y esperando la confirmación del
usuario entre pasos.

La estructura prevista permite ubicar cada clase cuando haga falta; no autoriza
crear a la vez sus entidades, repositorios, servicios y controllers.

## Revisión documental

Antes de cerrar la tarea #5 se comprueba que:

- Todas las funcionalidades tienen una ubicación identificada.
- Entidades, repositorios, servicios, controllers y DTOs tienen responsabilidades
  y ubicación claras.
- Autenticación, seguridad, excepciones y configuración están diferenciadas.
- Las pruebas tienen una ubicación y finalidad definidas.
- El documento y el ADR coinciden con el paquete base y el dominio aprobados.
- La rama contiene únicamente documentación de esta tarea; no implementación.

## Referencias

- [Arquitectura general](overview.md).
- [Modelo de dominio](domain-model.md).
- [Convenciones](conventions.md).
- [ADR-0007](../adr/0007-backend-package-organization.md).
- [Tarea #5](https://github.com/CristianPG1993/securebrowser-platform/issues/5).
