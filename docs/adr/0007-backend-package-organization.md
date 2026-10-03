# ADR-0007: Organización del backend por funcionalidad

- Estado: Aceptada
- Fecha: 2026-10-03

## Contexto

La tarea #5 define la organización interna del backend antes de generar clases
o paquetes. El backend utilizará Java y Spring Boot, con Spring Web, Spring
Security, Spring Data JPA, Hibernate y PostgreSQL.

El dominio corporativo del MVP está aprobado. Se necesita una ubicación clara
para entidades, repositorios, servicios, controllers, DTOs, seguridad,
configuración, excepciones y pruebas, manteniendo un desarrollo incremental.

La decisión fue aprobada junto con las restantes propuestas de diseño. Su
documentación y revisión se realizan en una rama y PR propias de la tarea #5;
esa aprobación no sustituye el seguimiento de las tareas #6, #7 y #8.

## Opciones consideradas

| Opción | Ventajas | Costes |
| --- | --- | --- |
| Paquetes globales por capa | Ubicación inicial sencilla de controllers, servicios y repositorios. | Una funcionalidad se reparte entre varios paquetes globales que crecen con todo el dominio. |
| Paquetes por funcionalidad | Reúne el código de cada área y mantiene visible su responsabilidad. | Requiere distinguir la configuración compartida y coordinar casos de uso entre funcionalidades. |
| Módulos y separación adicional por puertos y adaptadores | Permite imponer límites más fuertes y sustituir implementaciones. | Añade interfaces, módulos y conversiones que no se necesitan para el alcance actual. |

## Decisión

El paquete base será `com.securebrowser.platform`, con la clase de arranque en
ese paquete. El backend se organizará por funcionalidad:

- `company`, `user`, `license`, `enrollment`, `device`, `policy` y `securityevent`.
- `auth` para la entrada y coordinación de la autenticación.
- `security` para la integración con Spring Security y el usuario autenticado.
- `config` para configuración técnica compartida.
- `exception` para traducción centralizada de errores HTTP y excepciones compartidas.

Cada funcionalidad reúne su entidad, repositorio, servicio y controller cuando
los necesite. Sus DTOs se sitúan en un subpaquete `dto`. UrlRule, DownloadRule y
FilterMode pertenecen a `policy`; UserRole a `user`; SecurityEventType a
`securityevent`.

Controllers gestionan HTTP y validación de entrada; servicios coordinan casos
de uso, permisos, reglas y transacciones; repositorios gestionan consultas y
persistencia. Las entidades no se exponen como respuestas HTTP. Los servicios
pueden coordinar recursos de otras funcionalidades sin duplicar las reglas ni
crear dependencias circulares.

Las dependencias se proporcionan mediante constructores. Los servicios serán
clases concretas, sin crear sistemáticamente interfaces y clases `Impl`.
No se añaden módulos, microservicios ni capas genéricas a esta estructura.

La configuración de ejecución se sitúa en `src/main/resources` y las migraciones
Flyway en `src/main/resources/db/migration` cuando se implementen. Los secretos
se suministran mediante variables de entorno. La configuración de Spring
Security pertenece a `security`.

Las pruebas reproducen los paquetes de producción bajo `src/test/java`.
Se prevén JUnit 5 y Mockito para servicios, MockMvc para HTTP y PostgreSQL real
para comprobar persistencia, transacciones y concurrencia. Los recursos de
prueba se sitúan en `src/test/resources`.

El detalle y los ejemplos de ubicación se mantienen en
[Organización interna del backend](../architecture/backend-structure.md).

## Consecuencias

- El código de una funcionalidad se encuentra en una ubicación común.
- Las responsabilidades de HTTP, negocio y persistencia siguen diferenciadas.
- Los DTOs permiten evolucionar el contrato sin exponer entidades JPA.
- La configuración compartida y la seguridad tienen una ubicación identificada.
- La inyección por constructor permite probar servicios con dependencias sustituidas.
- Los casos de uso que abarcan varias funcionalidades requieren coordinación
  explícita y deben conservar las garantías del ADR-0006.
- Las pruebas con mocks no sustituyen la comprobación de integridad y concurrencia
  sobre PostgreSQL real.
- Los paquetes y clases se crean cuando se implementan; esta tarea solo documenta
  su organización. No se crean varias capas a la vez.

## Validación

La revisión documental comprobará que la estructura tiene una ubicación para
todas las funcionalidades, DTOs, seguridad, configuración, excepciones y tests,
y que cada responsabilidad coincide entre este ADR y el documento de estructura.

Durante la implementación se comprobará que las clases respetan su ubicación,
los controllers delegan las reglas al servicio, las entidades no se devuelven
directamente por HTTP y las pruebas ejercitan decisiones y garantías relevantes.

## Referencias

- [Organización interna del backend](../architecture/backend-structure.md).
- [Arquitectura general](../architecture/overview.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [Convenciones](../architecture/conventions.md).
- [Integridad y enrollment](0006-domain-integrity-and-enrollment.md).
- [Tarea #5](https://github.com/CristianPG1993/securebrowser-platform/issues/5).
