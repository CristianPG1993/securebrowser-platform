# ADR-0015: Producción y entrega de eventos del Security Engine

- Estado: Aceptada
- Fecha: 2026-11-08

## Contexto

El Security Engine debe producir `URL_BLOCKED`, `DOWNLOAD_BLOCKED` y
`POLICY_UPDATED` al aplicar las operaciones correspondientes. Evaluar una regla
puede repetirse antes de aplicar una decisión y no equivale a producir un suceso.

Los ADR-0003 y ADR-0004 ya establecen la persistencia offline, la identidad estable
del evento y su pertenencia a un Device. Este ADR concreta la responsabilidad del
motor, la semántica de actualización y el contenido entregado a esos bloques.

## Opciones consideradas

| Aspecto | Alternativas y consecuencias |
| --- | --- |
| Momento de producción | Emitir desde cada evaluación simplifica la llamada, pero puede registrar decisiones todavía no aplicadas y duplicar un bloqueo. Emitir al aplicar la operación representa el suceso efectivo. |
| Entrega | Gestionar SQLite, HTTP y una cola dentro del motor reúne todo el flujo, pero acopla evaluación, persistencia y transporte. Un receptor inyectado permite probar y componer esas responsabilidades por separado. |
| Cambio de política | Emitir ante cada consulta, primera carga o cambio de metadatos registra actividad de adquisición. Comparar identidad y configuración representa los cambios de seguridad efectivamente aplicados. |
| Identidad del bloqueo | Agrupar por URL, nombre o proximidad temporal puede confundir intentos distintos. Correlacionar las notificaciones de la operación permite producir un evento por bloqueo. |
| Detalle | El texto libre es sencillo de leer, pero exige interpretar frases. JSON compacto dentro de la cadena existente permite códigos y campos estables sin modificar el contrato REST. |

## Decisión

En la tarea #114 se adoptaron las siguientes decisiones.

### Producción y receptor

Los evaluadores puros devolverán decisiones sin producir eventos como efecto
secundario. Al aplicar un bloqueo se producirá el evento correspondiente, una
sola vez por operación. Intentos distintos producirán eventos distintos aunque
coincidan la URL o el nombre de archivo. La correlación concreta de notificaciones
se definirá durante la integración con CEF.

El motor entregará el evento a un receptor pequeño inyectado y no mantendrá una
cola interna de pendientes. SQLite y sincronización HTTP corresponderán a los
bloques Offline y REST. Un fallo de entrega será observable, mantendrá el bloqueo
y no se presentará como persistencia o sincronización correctas.

Cancelar voluntariamente «Guardar como» no producirá `DOWNLOAD_BLOCKED`.

### Semántica de `POLICY_UPDATED`

La primera política válida de cada sesión no producirá el evento. Sustituir una
política anterior por otra diferente lo producirá después de aplicar correctamente
la sustitución. Una respuesta inválida, rechazada, descartada por contexto o cuya
aplicación falle no lo producirá.

Se compararán el `id`, los indicadores de activación, los modos y las reglas
normalizadas, sin considerar el orden de estas. Cambiar el `id` contará como
cambio aunque la configuración coincida. La compañía se validará contra el
contexto de la instalación antes de comparar.

Cambiar únicamente `name`, `createdAt` o `updatedAt` actualizará los metadatos
sin producir el evento. Una obtención y validación correctas renovarán la vigencia
local aunque la política y su `updatedAt` no cambien, sin producir por ello
`POLICY_UPDATED`.

### Datos e identidad de origen

`SecurityEvent` conservará `eventUuid`, `type`, `occurredAt` y `details`, compatibles
con la petición existente. El UUID se generará una sola vez. `occurredAt` utilizará
la hora del sistema, en UTC y con precisión de microsegundos; la vigencia de
políticas seguirá utilizando un reloj monotónico.

El evento quedará fijado al producirse. Los reenvíos conservarán UUID, tipo,
instante y contenido serializado, sin reconstruirlos con información posterior.

El receptor recibirá también el contexto de sesión e instalación de origen,
separado de los campos REST. No asociará automáticamente el evento a la sesión
que esté activa después. El motor no añadirá propietarios al cuerpo REST ni
asignará `receivedAt`, que corresponde al backend.

### Formato y contenido de `details`

`details` será una cadena que contiene JSON compacto con `schemaVersion: 1` y
un código controlado en `reason`. Incluirá `policyId` cuando esté disponible y
el contexto mínimo pertinente: host interpretado en `URL_BLOCKED`, extensión
evaluada en `DOWNLOAD_BLOCKED` e identificación de la política aplicada en
`POLICY_UPDATED`.

Por ejemplo, el contenido de la cadena podrá ser:

```json
{"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7,"host":"example.com"}
```

No incluirá URL completas, credenciales, rutas, parámetros, nombres completos de
archivos ni reglas completas de políticas. Una URL no interpretable o un archivo
sin extensión conservarán su motivo sin recurrir al texto original como alternativa.
No se inventarán identificadores de política ausentes.

El contenido respetará el límite de 2.000 puntos de código Unicode. En la petición
HTTP seguirá siendo una cadena correctamente escapada, no un objeto de la API.

## Consecuencias

- El núcleo podrá probarse con GoogleTest y un receptor de captura sin Qt,
  Chromium, SQLite ni backend.
- La captura o entrega de un evento no acreditará almacenamiento durable ni
  sincronización; cada bloque necesitará su evidencia.
- La integración deberá correlacionar callbacks sin agrupar intentos distintos
  por su URL, nombre o proximidad temporal.
- Offline y REST deberán conservar el contenido original y la instalación de
  origen para mantener la semántica de reenvío del backend.
- Los mensajes de interfaz podrán cambiar sin modificar los códigos de motivo.
- La persistencia offline acordada en el ADR-0003 sigue siendo necesaria para
  completar el MVP; corresponde al bloque de almacenamiento y sincronización.

## Validación

La implementación y las pruebas están pendientes. Se comprobará que:

- Evaluar no produce eventos y aplicar un bloqueo produce uno por operación.
- Dos intentos distintos con igual URL o nombre reciben UUID diferentes.
- Cancelar voluntariamente «Guardar como» no produce un evento de bloqueo.
- Un fallo del receptor es observable y conserva la decisión de bloqueo.
- `POLICY_UPDATED` respeta la comparación de identidad y configuración, las
  exclusiones de primera carga y metadatos y la aplicación correcta del cambio.
- La entrega conserva el contexto de origen aunque después cambie la sesión.
- Los reenvíos conservan UUID, tipo, instante y detalle serializado.
- `details` contiene JSON válido dentro de una cadena, cumple el límite y
  excluye los datos que no deben registrarse.
- Las pruebas de Offline y REST acreditan por separado persistencia, reenvío
  y confirmación, sin atribuir esos resultados al receptor de captura.

## Referencias

- [Tarea #114: contratos, límites y decisiones del MVP](https://github.com/CristianPG1993/securebrowser-platform/issues/114).
- [Diseño del Security Engine](../architecture/security-engine.md).
- [Contratos de la API REST](../architecture/rest-api-contracts.md).
- [ADR-0003: persistencia local y sincronización de eventos](0003-offline-security-events.md).
- [ADR-0004: relación de eventos con dispositivos y compañías](0004-security-event-ownership.md).
- [ADR-0014: selección de destino y autorización de descargas](0014-desktop-download-authorization.md).
