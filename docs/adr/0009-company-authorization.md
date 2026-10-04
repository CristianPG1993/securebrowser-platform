# ADR-0009: Autorización por Company y propietario

- Estado: Aceptada
- Fecha: 2026-10-04

## Contexto

La tarea #7 define la autorización del MVP corporativo. Cada User pertenece
a una Company y tiene rol ADMIN o USER. La autenticación del ADR-0008 obtiene
el usuario, Company y rol actuales desde la base de datos en cada petición.

Un rol por sí solo no impide acceder a recursos de otra compañía ni a Devices
de otro usuario. Las operaciones deben combinar permiso de rol y pertenencia,
manteniendo las restricciones de integridad y concurrencia del ADR-0006.
Desktop necesita registrar su instalación, obtener su política y enviar eventos;
la administración permite gestionar recursos de la compañía sin representar
a otro usuario durante esas operaciones de instalación.

## Opciones consideradas

| Aspecto | Opción elegida | Alternativa y compromiso |
| --- | --- | --- |
| Ámbito del ADMIN | Su propia Company. | Un administrador global requiere permisos y operaciones ajenos al MVP. |
| Acceso de USER | Perfil, Devices y eventos propios; política asignada a su Device activo. | Dar acceso general a la Company expone recursos de otros usuarios y amplía sus funciones. |
| Pertenencia | Consultas limitadas por Company y propietario, coordinadas por el servicio. | Comprobar solo el rol deja sin proteger los identificadores de recursos. |
| Listados | Aplicar el ámbito en la consulta, incluidos totales y paginación. | Filtrar una página obtenida globalmente produce resultados y recuentos incorrectos. |
| Recursos ajenos | HTTP 404 en operaciones que el rol permite. | Distinguir entre recurso inexistente y ajeno revela su existencia. |
| Modelo de permisos | Dos roles y reglas explícitas de pertenencia. | Permisos configurables y tablas de ACL añaden gestión que no necesita este MVP. |

## Decisión

### ADMIN y USER

ADMIN consulta y edita su Company y gestiona sus usuarios, licencias, enrollments,
Devices y políticas, respetando las reglas del dominio. Consulta los eventos
de los Devices de su Company. No puede trasladar recursos entre compañías,
eliminar Company ni acceder a recursos de otra Company.

USER consulta y edita su perfil, cambia su contraseña y consulta sus Devices
y sus eventos. No administra la Company, otros usuarios, licencias, enrollments,
Devices o políticas. Su perfil no admite cambiar role ni company.

Ambos roles consumen únicamente enrollments de los que sean destinatarios,
obtienen políticas para sus propios Devices activos y envían eventos solo desde
sus propios Devices. ADMIN puede consultar políticas mediante su acceso de
administración, pero ese permiso no autoriza generar eventos ni registrar una
instalación en nombre de otro usuario.

Un Device inactivo puede conservar acceso a su historial y sincronizar eventos
pendientes de su propietario. No obtiene nuevas políticas, no se reactiva por
sincronizar y no vuelve a ocupar un puesto de licencia.

La creación de Company con su primer ADMIN queda como operación interna,
sin registro público de compañías en el MVP.

### Company y propietario

El contexto autenticado determina Company y User.id. No se confía en Company,
identidad o rol enviados por el cliente. Se comprueba la pertenencia tanto del
recurso principal como de sus referencias y recursos anidados.

SecurityEvent obtiene su Company y propietario a través de Device; UrlRule
y DownloadRule a través de Policy. El consumo de enrollment comprueba User,
License y Policy inicial y exige que el usuario sea el destinatario. Los
reintentos y la deduplicación conservan los mismos controles de acceso.

Los detalles, listados, filtros y totales se consultan dentro del ámbito
permitido. Las operaciones no autorizadas no devuelven registros ajenos ni
modifican recursos o capacidad. Los DTOs admiten solo los campos editables
de cada operación; los valores administrados por el servidor no se vinculan
directamente a la entidad desde una petición.

### Respuestas y responsabilidades

Se comprueba autenticación, permiso de operación, pertenencia y reglas de
dominio en ese orden. Falta de autenticación produce 401; un rol no permitido
produce 403 sin revelar la existencia del recurso. Un recurso que no existe
en el ámbito permitido produce 404, igual que un id inexistente.

Spring Security protege peticiones y permisos de métodos; los servicios
resuelven recursos dentro de su ámbito y aplican pertenencia y reglas de negocio
antes de producir efectos. La seguridad de métodos se habilitará expresamente
durante la implementación, usando autoridades derivadas del User actual.

Las escrituras mantienen sus transacciones y bloqueos, releyendo y validando
los recursos después de adquirir los bloqueos exigidos por el dominio.
La autorización no sustituye las garantías de capacidad, último ADMIN,
eliminación restringida ni revocación de renovación.

## Consecuencias

- ADMIN tiene administración corporativa sin convertirse en administrador global.
- USER accede a sus recursos sin obtener un catálogo de recursos de compañeros.
- Administrar un Device y comunicarse como su propietario son permisos distintos.
- Las consultas incluyen Company y propietario según la operación, también en
  totales, referencias anidadas, reintentos y sincronización.
- Los recursos ajenos quedan ocultos mediante el mismo 404 que un id inexistente.
- La implementación conserva dos roles sin añadir entidades ni atributos.
- La tarea documenta reglas y pruebas; los contratos REST completos siguen en
  la #8 y el código se desarrollará clase a clase posteriormente.

## Validación

Se revisa que la matriz de permisos y este ADR coincidan con la autenticación
y el modelo de dominio. Los casos detallados se recogen en el documento de
autorización y se implementarán cuando exista código.

Se probarán ADMIN y varios USER en al menos dos Companies, cubriendo acceso
permitido, acceso entre compañías, recursos de otro propietario, referencias
anidadas, listas y totales, consumo y reintento de enrollment, políticas de
Devices activos e inactivos, eventos pendientes y ausencia de escrituras ante
rechazos. También se comprobarán cambios de rol con JWT vigente y restricciones
del último ADMIN. Persistencia y concurrencia requieren PostgreSQL real.

## Referencias

- [Autorización por Company](../architecture/company-authorization.md).
- [Autenticación JWT](../architecture/jwt-authentication.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [ADR-0006](0006-domain-integrity-and-enrollment.md).
- [ADR-0008](0008-jwt-authentication.md).
- [Seguridad de métodos de Spring Security](https://docs.spring.io/spring-security/reference/servlet/authorization/method-security.html).
- [OWASP: autorización](https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html).
- [OWASP: prevención de acceso por identificadores ajenos](https://cheatsheetseries.owasp.org/cheatsheets/Insecure_Direct_Object_Reference_Prevention_Cheat_Sheet.html).
- [Tarea #7](https://github.com/CristianPG1993/securebrowser-platform/issues/7).
