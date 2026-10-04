# Autorización por Company de SecureBrowser Platform

## Alcance

Este documento corresponde a la tarea #7 y define los permisos de ADMIN y USER
antes de implementar Spring Security y los servicios. La decisión se recoge
en el [ADR-0009](../adr/0009-company-authorization.md).

Todo User pertenece a una Company. ADMIN administra únicamente su compañía;
no existe un administrador global ni registro público de compañías en el MVP.
La creación interna de Company y su primer ADMIN sigue siendo atómica.

La [autenticación JWT](jwt-authentication.md) proporciona User.id, Company.id
y el rol actuales desde la base de datos. La autorización utiliza ese contexto,
sin aceptar identidad, Company o permisos alternativos enviados por el cliente.
Los [contratos REST](rest-api-contracts.md) concretan endpoints, DTOs y errores
en la #8.
Esta tarea no crea clases, configuración ejecutable ni nuevas entidades.

## Reglas comunes

Una operación requiere autenticación, un rol permitido y pertenencia del recurso
al ámbito autorizado. Ser ADMIN no sustituye la comprobación de Company.
Cuando la operación exige propiedad individual, se comprueba también User.id.

Como referencia conceptual:

```text
administrar recurso:
    usuario.role == ADMIN
    recurso.company.id == usuario.company.id

operar como propietario de un Device:
    device.company.id == usuario.company.id
    device.user.id == usuario.id
```

Se deniega cualquier operación no permitida expresamente. Los permisos se
comprueban antes de devolver datos o modificar registros, también en consultas
de listas, filtros, relaciones anidadas y reintentos.

El permiso no elimina las restricciones del
[modelo de dominio](domain-model.md): último ADMIN, capacidad de License,
inmutabilidad de asociaciones, eliminación restringida e historial conservado.

## Matriz de permisos

ADMIN dispone también de las operaciones sobre su propio perfil y dispositivos.
Ese acceso individual conserva los mismos requisitos de propiedad que USER.

| Recurso u operación | ADMIN | USER |
| --- | --- | --- |
| Company | Consultar y editar su propia Company. | Sin acceso a su administración. |
| Usuarios | Crear, consultar, editar y eliminar usuarios de su Company; gestionar roles respetando el último ADMIN. | Consultar y editar únicamente su propio perfil. |
| Contraseña propia | Cambiarla según el flujo de autenticación. | Cambiarla según el mismo flujo. |
| License | Crear, consultar, modificar y eliminar licencias de su Company dentro de las reglas del dominio. | Sin acceso directo. |
| EnrollmentToken | Emitir, consultar y revocar enrollments de su Company. | Sin operaciones de administración. |
| Consumir enrollment | Solo cuando sea el usuario destinatario. | Solo cuando sea el usuario destinatario. |
| Device | Consultar los de su Company; modificar nombre, política y actividad conforme al dominio. | Consultar únicamente sus propios Devices. |
| Policy y sus reglas | Crear, consultar, editar y eliminar políticas de su Company; asignarlas a Devices de esa Company. | Sin acceso al catálogo ni a la edición de políticas. |
| Obtener política para una instalación | Solo para sus propios Devices activos mediante la operación de instalación. Puede consultar políticas de su Company por la operación de administración. | Solo la política actualmente asignada a uno de sus Devices activos. |
| Consultar SecurityEvent | Consultar eventos de Devices de su Company. | Consultar eventos de sus propios Devices. |
| Enviar SecurityEvent | Solo para sus propios Devices, incluso si están inactivos. | Solo para sus propios Devices, incluso si están inactivos. |

Consultar un Device propio o sus eventos no exige que esté activo. En cambio,
la obtención de nuevas políticas por la instalación exige Device.active = true.
La desactivación no borra el historial ni impide sincronizar eventos pendientes;
esa sincronización no reactiva el Device ni vuelve a ocupar capacidad.

## Perfil y administración de usuarios

El perfil propio permite editar name, lastName y email respetando validación
y unicidad. No permite editar role, company, passwordHash, id ni fechas de
auditoría. Cambiar la contraseña es una operación separada que utiliza BCrypt
y revoca los grupos de renovación conforme al diseño de autenticación.

ADMIN puede gestionar los datos y el rol de usuarios de su Company mediante
operaciones administrativas. Tampoco puede trasladarlos a otra Company ni
obtener sus contraseñas, hashes o secretos de renovación. La creación de un
usuario asigna la Company desde el contexto autenticado y trata su contraseña
según las reglas de autenticación.

Un ADMIN puede modificar su propio rol o solicitar su propio borrado solo si
las restricciones del dominio lo permiten. Los cambios de rol y borrados
mantienen el bloqueo de Company y la regla de conservar al menos un ADMIN.
Las peticiones posteriores usan el rol actualizado de la base de datos.

## Ámbito de consulta y relaciones

| Recurso | Cómo se determina Company y propiedad individual |
| --- | --- |
| Company | Su id debe coincidir con la Company del usuario autenticado. |
| User, License, Policy | Su relación Company debe coincidir con la del contexto. Para perfil propio, User.id debe coincidir también. |
| Device | Su Company debe coincidir; cuando se exige propiedad individual, también Device.user.id. |
| EnrollmentToken | Su User, License y Policy inicial deben pertenecer a la Company del contexto; el consumo exige además EnrollmentToken.user.id = usuario.id. |
| UrlRule y DownloadRule | Su Company se obtiene mediante Policy. La regla debe pertenecer a la política de la operación. |
| SecurityEvent | Su Company y propietario se obtienen mediante Device. El evento debe pertenecer al dispositivo de la operación cuando se indique uno. |
| RefreshToken | Se mantiene bajo el flujo de autenticación: pertenece a User y no es un recurso administrativo que conceda acceso a otras cuentas. |

Los listados y sus totales se calculan dentro del ámbito autorizado desde la
consulta a PostgreSQL. No se recupera una lista global para filtrarla después
de paginar. ADMIN obtiene recursos de su Company y USER únicamente los propios
cuando tiene permiso de listado. Un filtro no puede ampliar ese ámbito.

Para consultar un detalle se busca el id dentro del ámbito permitido. Los
identificadores de una ruta, cuerpo o filtro no constituyen una prueba de
propiedad. El mismo criterio se aplica a reglas y eventos aunque el recurso
padre indicado sí pertenezca al usuario o a su Company.

En una modificación se resuelven también las referencias relacionadas dentro
del ámbito autorizado. Por ejemplo, asignar una Policy a un Device exige que
ambos pertenezcan a la Company del ADMIN. Emitir un enrollment exige comprobar
User, License y Policy inicial antes de reservar capacidad.

Los DTOs solo admiten campos editables de la operación. La Company de nuevos
recursos procede del contexto; el propietario de un Device y sus asociaciones
de origen proceden del enrollment. No se vincula indiscriminadamente un cuerpo
de petición con una entidad persistente.

## Enrollment y envío de eventos

El secreto de enrollment autoriza una instalación del usuario destinatario,
pero no sustituye su autenticación. Ningún ADMIN puede consumir el token de
otro usuario para registrar un Device en su nombre.

Un reintento conserva los controles de Company y destinatario. Solo el mismo
usuario y deviceIdentifier reciben el Device existente, sin modificar la
política actual, usedAt o actividad. Se mantienen las reglas de caducidad,
capacidad y unicidad del modelo.

El envío de eventos exige comprobar la Company y el propietario del Device
antes de crear registros o reconocer un reenvío. ADMIN puede consultar eventos
de otro usuario de su Company, pero no generarlos en su nombre. El servidor
asigna Device desde el recurso autorizado y receivedAt desde su reloj.

La deduplicación por eventUuid nunca devuelve un evento fuera del ámbito
autorizado. Un UUID reutilizado con un Device o contenido distinto continúa
siendo un conflicto, sin exponer el contenido del registro ajeno. Una petición
rechazada por permisos no crea eventos ni cambia la actividad del Device.

## Respuestas y orden de comprobación

| Situación | Resultado |
| --- | --- |
| Falta autenticación válida | HTTP 401, conforme al diseño de autenticación. |
| El rol no permite la operación | HTTP 403, sin consultar o revelar la existencia del recurso solicitado. |
| El rol permite la operación, pero el recurso no existe en su ámbito | HTTP 404; misma respuesta para id inexistente y recurso ajeno. |
| Regla o evento no pertenece al padre autorizado de la operación | HTTP 404. |
| Identificador relacionado fuera del ámbito permitido | HTTP 404, sin aplicar cambios. |
| Permisos correctos, pero una regla de dominio impide la operación | Rechazo de negocio; su contrato completo se define en la #8. |

El orden es autenticar, comprobar permiso de operación, resolver recursos en
su ámbito y validar las reglas de dominio. Por ejemplo, USER recibe 403 al
intentar administrar cualquier licencia; ADMIN recibe 404 al consultar una
licencia de otra Company. Los errores no incluyen Company, propietario ni
contenido de recursos ajenos. El cliente no intenta renovar tokens por un 403.

## Implementación posterior en Spring Security y servicios

Spring Security validará el Bearer JWT y construirá el contexto actual según
la #6. Solo las operaciones públicas explícitas de autenticación se permiten
sin JWT; renovación y logout siguen exigiendo su secreto según aquel diseño.
Las rutas restantes se protegen y se deniegan las que no estén contempladas.

Se prevé seguridad de métodos para permisos de rol en los servicios, activada
expresamente cuando se implemente. Las autoridades se construirán desde el
User actual como ROLE_ADMIN o ROLE_USER, sin extraerlas de claims del JWT.
La comprobación de rol no sustituye las consultas con Company y propietario
ni las validaciones explícitas de pertenencia dentro del servicio.

Las consultas de repositorio reciben el ámbito decidido por el servicio.
Los controllers obtienen el contexto autenticado y delegan; no permiten que
un parámetro del cliente determine la Company del contexto.

Las escrituras comprueban pertenencia y negocio dentro de su transacción,
volviendo a leer los recursos protegidos por los bloqueos que ya exige el
modelo. No se confía únicamente en una validación previa del controller ni
en un resultado obtenido antes de esperar un bloqueo. Se mantienen las
garantías de Company, License, Policy y User documentadas en las #4 y #6.

No se añaden permisos configurables, un administrador global, tablas de ACL
ni infraestructura adicional de compañías. Las clases y métodos se desarrollan
paso a paso después de las tareas de diseño.

## Casos de prueba previstos

Se utilizarán al menos dos Companies, con un ADMIN y varios USER en cada una,
Devices activos e inactivos, licencias, políticas, enrollments y eventos.
Las pruebas incluirán acceso permitido y denegado para cada fila de la matriz.

| Caso | Resultado que debe comprobarse |
| --- | --- |
| ADMIN gestiona recursos de su Company | Acceso permitido, conservando las restricciones del dominio. |
| ADMIN solicita un recurso de otra Company | HTTP 404 y ninguna lectura o modificación expuesta. |
| USER intenta administrar Company, usuarios, licencias, enrollments o políticas | HTTP 403, también con ids inexistentes o ajenos. |
| USER consulta su perfil, Devices o eventos | Solo obtiene los recursos propios. |
| USER consulta otro User o Device en una operación individual permitida | HTTP 404, aunque pertenezca a su Company. |
| Perfil propio intenta modificar role, company o campos del servidor | No persiste cambios en esos campos ni obtiene permisos adicionales. |
| ADMIN cambia roles o elimina usuarios | Mantiene al menos un ADMIN, también ante operaciones simultáneas. |
| Rol del usuario cambia conservando un JWT vigente | La petición siguiente aplica el rol actual. |
| Listas, totales, páginas y filtros | No incluyen recursos ni recuentos fuera del ámbito autorizado. |
| ADMIN asigna Policy o emite enrollment con referencias de otra Company | HTTP 404 y ningún cambio ni reserva de capacidad. |
| Regla o evento de otro padre | HTTP 404 aunque ambos padres pertenezcan a la misma Company. |
| Destinatario consume su enrollment | Crea su Device con las asociaciones del token; no acepta otros propietarios. |
| Otro usuario o ADMIN consume o reintenta un enrollment ajeno | Rechazo sin Device nuevo, consumo de token ni cambios de capacidad. |
| Reintento autorizado de enrollment | Devuelve únicamente su Device original sin reactivarlo ni cambiar su política. |
| Propietario obtiene política de su Device activo | Devuelve únicamente la Policy actualmente asignada. |
| Instalación inactiva solicita una política nueva | Rechazo sin alterar Device.active; la consulta administrativa de Policy permanece disponible para ADMIN. |
| Propietario consulta un Device inactivo o sus eventos | Acceso permitido al historial propio. |
| Propietario envía eventos desde Device inactivo | Se conservan sin reactivar ni ocupar un puesto. |
| ADMIN o USER envía eventos de otro propietario | HTTP 404, sin crear eventos ni revelar registros existentes. |
| Reenvío o conflicto de eventUuid | Conserva la deduplicación sin exponer eventos ajenos. |
| Recurso inexistente y recurso fuera del ámbito | Mismo HTTP 404 cuando el rol permite la operación. |
| Petición protegida sin JWT válido | HTTP 401 antes de consultar recursos. |
| Invocación del servicio sin pasar por el controller | Conserva controles de rol, Company y propiedad. |
| Operación denegada | No modifica recursos, reglas, capacidad, fechas de consumo ni eventos. |

Las pruebas de servicio comprueban decisiones de permisos y pertenencia; las
pruebas HTTP verifican autenticación y respuestas 401, 403 y 404. Las consultas,
totales, transacciones, bloqueos y ausencia de escrituras ante rechazos se
verificarán con PostgreSQL real. La seguridad de métodos se comprobará mediante
el servicio gestionado por Spring, sin asumir que un objeto creado directamente
en una prueba aplica sus interceptores.

La revisión actual valida documentación y coherencia; estos casos todavía
no se han implementado ni ejecutado.

## Referencias

- [ADR-0009](../adr/0009-company-authorization.md).
- [Autenticación JWT](jwt-authentication.md).
- [Contratos iniciales de la API REST](rest-api-contracts.md).
- [Modelo de dominio](domain-model.md).
- [Organización del backend](backend-structure.md).
- [Seguridad de métodos de Spring Security](https://docs.spring.io/spring-security/reference/servlet/authorization/method-security.html).
- [OWASP: autorización](https://cheatsheetseries.owasp.org/cheatsheets/Authorization_Cheat_Sheet.html).
- [OWASP: prevención de acceso por identificadores ajenos](https://cheatsheetseries.owasp.org/cheatsheets/Insecure_Direct_Object_Reference_Prevention_Cheat_Sheet.html).
- [Tarea #7](https://github.com/CristianPG1993/securebrowser-platform/issues/7).
