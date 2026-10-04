# ADR-0010: Contratos REST del MVP

- Estado: Aceptada
- Fecha: 2026-10-04

## Contexto

La tarea #8 define cómo se comunican Desktop y Android con el backend antes
de implementar controllers. Las entidades, pertenencias, autenticación con
renovación y autorización por Company ya están aprobadas en las tareas previas.

Los clientes necesitan rutas, DTOs, estados HTTP y errores previsibles para
administrar la compañía o comunicarse como propietario de una instalación.
El contrato debe conservar las garantías de capacidad, último ADMIN, consumo
único de enrollment y sincronización offline de eventos.

## Opciones consideradas

| Aspecto | Opción elegida | Alternativa y compromiso |
| --- | --- | --- |
| Interfaz | JSON sobre HTTPS bajo /api/v1. | Rutas sin versión no reservan un espacio explícito para cambios futuros de contrato. |
| Datos públicos | DTOs propios con campos permitidos. | Serializar entidades JPA expone estructura persistente, asociaciones y posibles secretos. |
| Actualizaciones | PATCH con campos opcionales de un DTO propio. | Exigir reemplazos completos obliga a reenviar campos que no se editan. |
| Reglas de Policy | Listas de dominios y extensiones reemplazables dentro de la política. | Endpoints por regla añaden operaciones para una configuración que debe mantenerse coherente. |
| Listados | Paginación común con filtros limitados. | Listados sin límite crecen sin control; filtros y orden arbitrarios amplían validaciones innecesarias para el MVP. |
| Eventos | Lotes atómicos con ACK de UUID. | Éxito parcial exige al cliente distinguir elementos fallidos dentro de una misma respuesta. |
| Errores | Problem Details con extensión code estable. | Formatos distintos por controller o texto libre dificultan el tratamiento uniforme de clientes. |

## Decisión

### Rutas y permisos

Se utilizan las rutas documentadas bajo /api/v1 para auth, companies, users,
licenses, enrollments, devices, policies y security-events.
Solo login, refresh y logout no exigen JWT vigente; mantienen sus pruebas
de credenciales o secreto. Las demás operaciones utilizan el contexto actual
de User, Company y role de la base de datos.

Company se obtiene mediante /companies/me y se administra solo por ADMIN.
El perfil propio utiliza /users/me; la contraseña se cambia mediante una
operación separada con contraseña actual y nueva. Los recursos administrativos
respetan el aislamiento de Company y las operaciones de instalación exigen
propiedad individual, conforme al ADR-0009.

Device se crea solo al consumir enrollment y se desactiva en lugar de borrarse.
USER obtiene su política mediante su Device activo; ADMIN mantiene además
la consulta administrativa de políticas de su Company. Los eventos son
inmutables y se pueden sincronizar desde Devices inactivos de su propietario.
No se añade registro público ni administración global de compañías.

### DTOs, validación y listas

Los campos se representan en camelCase, IDs como enteros positivos, UUID y
enums como cadenas, y fechas ISO 8601 UTC. Company procede del contexto;
propietario y relaciones de origen de Device proceden del enrollment.
Las respuestas no contienen hashes, contraseñas ni secretos administrativos.

En PATCH se conservan campos ausentes. Solo un campo opcional admite null;
listas presentes sustituyen la lista completa y [] vacía esa lista. Los campos
desconocidos, readonly, tipos y valores inválidos se rechazan con 400.
La configuración y reglas de Policy se confirman en una única transacción.

Las listas utilizan PageResponse con items, page, size, totalElements y
totalPages. page empieza en 0; size inicial es 20 y máximo 100. Filtros,
orden y totales se aplican dentro del ámbito autorizado en PostgreSQL.

### Resultados, reintentos y sincronización

Se responde 200 para consultas, modificaciones con recurso resultante,
login, refresh, lotes y reintentos reconocidos de enrollment. La creación
responde 201 con recurso y Location; borrado permitido, revocación, logout
y cambio de contraseña responden 204 sin cuerpo.

Consumir el mismo enrollment como su destinatario y con el mismo UUID
de instalación devuelve el Device original, sin ocupar otro puesto, alterar
la política o reactivar la instalación. La emisión entrega el secreto por
email después de confirmar la reserva; no promete atomicidad con el correo.

Los lotes de eventos tienen de 1 a 100 elementos. Se confirman atómicamente
y devuelven acceptedEventUuids únicos, incluidos reenvíos idénticos. Desktop
retira esos UUID de SQLite después de recibir confirmación. Un conflicto
o evento inválido impide confirmar eventos nuevos de todo el lote, conservando
los registros previamente existentes. Las respuestas se entregan tras commit.

Estos reintentos no introducen deduplicación general para todas las creaciones.
Ante resultado incierto se consulta el estado antes de repetir una emisión.

### Errores

Se usan 400 para validación, 401 para autenticación, 403 para rol no permitido,
404 para recurso inexistente o ajeno, 409 para conflictos del dominio y 503
para indisponibilidad temporal. Los errores inesperados conservan 500 y las
respuestas de protocolo conservan sus estados y cabeceras correspondientes.

El formato es Problem Details conforme a RFC 9457, con type, title, status,
detail, instance y code estable. La validación puede añadir errors con field
y message, sin valores sensibles. MVC y Spring Security comparten el formato.
El documento de contratos define los códigos concretos y los errores de cada
endpoint; las respuestas no revelan contenido ni propietarios ajenos.

## Consecuencias

- Desktop y Android disponen de un contrato común separado de JPA.
- Los DTOs impiden editar Company, propietario o campos del servidor mediante
  asignación directa de una petición a una entidad.
- La API mantiene permisos y restricciones de negocio de las tareas anteriores.
- El reemplazo de listas simplifica la edición de políticas y necesita conservar
  su actualización atómica y snapshot coherente.
- Los ACK por UUID permiten vaciar la cola local tras un commit confirmado.
- Los conflictos y errores de servicio son distinguibles mediante status y code.
- El cambio es documental; el proyecto mínimo y sus clases se implementarán
  posteriormente paso a paso, sin generar varias capas a la vez.

## Validación

Se revisa que cada endpoint tenga método, ruta, request, response, estado,
autenticación, autorización y errores esperados. Los DTOs y ejemplos se
contrastan con el dominio y los ADR-0008 y ADR-0009.

Los casos detallados se recogen en el documento de contratos. Al implementar
se probarán serialización, entradas, paginación, permisos entre compañías,
estados y cabeceras, cambio de contraseña, reintentos y lotes atómicos.
Persistencia, concurrencia y ausencia de escrituras ante rechazos requieren
PostgreSQL real. Esta revisión documental no ejecuta pruebas de backend.

## Referencias

- [Contratos iniciales de la API REST](../architecture/rest-api-contracts.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [Autenticación JWT](../architecture/jwt-authentication.md).
- [Autorización por Company](../architecture/company-authorization.md).
- [Organización del backend](../architecture/backend-structure.md).
- [ADR-0006](0006-domain-integrity-and-enrollment.md).
- [ADR-0008](0008-jwt-authentication.md).
- [ADR-0009](0009-company-authorization.md).
- [RFC 9110](https://www.rfc-editor.org/rfc/rfc9110.html).
- [RFC 9457](https://www.rfc-editor.org/rfc/rfc9457.html).
- [Spring MVC: respuestas de error](https://docs.spring.io/spring-framework/reference/web/webmvc/mvc-ann-rest-exceptions.html).
- [Tarea #8](https://github.com/CristianPG1993/securebrowser-platform/issues/8).
