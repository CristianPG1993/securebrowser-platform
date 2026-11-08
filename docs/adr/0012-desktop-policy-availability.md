# ADR-0012: Disponibilidad y vigencia local de la política en Desktop

- Estado: Aceptada
- Fecha: 2026-11-08

## Contexto

El Security Engine necesita una política válida asignada al dispositivo para
evaluar la navegación web y las descargas. Debe definir su comportamiento tanto
al arrancar sin política como al perder la conexión después de haberla obtenido.

El contrato existente `PolicyResponse` incluye `createdAt` y `updatedAt`, pero no
un campo de caducidad. El plazo acordado es una regla local de Desktop y no
modifica el contrato del backend.

La adquisición es asíncrona: una respuesta puede llegar después de terminar la
sesión que la solicitó. También debe distinguirse la pérdida temporal de conexión
de una denegación explícita de acceso a la instalación.

## Opciones consideradas

### Arranque sin política válida

- Permitir temporalmente navegación y descargas facilita el uso inmediato, pero
  deja un periodo sin las restricciones asignadas al dispositivo.
- Bloquear esas operaciones hasta disponer de política válida impide ese uso
  sin protección, a costa de necesitar obtener la política antes de navegar.

### Desconexión con una política previamente válida

- Bloquear inmediatamente evita utilizar reglas potencialmente antiguas, pero
  interrumpe la navegación ante cualquier corte de conexión.
- Conservar la política durante un plazo limitado permite tolerar cortes
  temporales, aunque los cambios del backend pueden tardar en aplicarse.
- Conservarla indefinidamente permite desconexiones prolongadas, pero mantiene
  restricciones antiguas sin un límite de tiempo.

Se consideraron plazos de 5, 15 y 60 minutos. Se eligieron 15 minutos como margen
inicial del MVP entre continuidad de uso y antigüedad de la política aplicada.

### Validación, contexto y denegaciones

- Confiar únicamente en una respuesta correcta del backend no cubre campos
  incompletos, errores de interpretación o respuestas de sesiones anteriores.
  Validar el conjunto y conservar el contexto solicitante permite rechazarlos.
- Identificar el contexto solo por usuario y dispositivo puede confundir dos
  sesiones sucesivas de la misma cuenta. Identificar la sesión concreta permite
  descartar sus respuestas cuando termina, aunque se vuelva a iniciar sesión.
- Aplicar la tolerancia temporal también a una denegación explícita permitiría
  seguir autorizando operaciones de una instalación rechazada. Retirar su
  autorización inmediatamente exige distinguir esa respuesta de un fallo temporal.

## Decisión

Las siguientes decisiones han sido aprobadas por el autor en la tarea #114:

### Disponibilidad y plazo local

- Al arrancar sin una política válida del dispositivo, se bloquearán las nuevas
  navegaciones HTTP/HTTPS y las descargas: comportamiento *fail closed*.
- La interfaz mostrará «Política de seguridad no disponible». El inicio de
  sesión, la comunicación REST necesaria para obtener la política y los usos
  internos de CEF necesarios para mostrar la aplicación permanecerán disponibles.
- Ante una desconexión, se conservará la última política válida en memoria para
  la misma sesión e instalación mientras no haya agotado su vigencia local.
- La vigencia será de 15 minutos desde la última obtención y validación correctas.
  Una consulta correcta renovará el plazo aunque la política no haya cambiado.
- Los fallos de conexión, las respuestas inválidas y la propia desconexión no
  renovarán el plazo ni iniciarán otro periodo de 15 minutos.
- El tiempo transcurrido se medirá con un reloj monotónico. Cambiar la hora de
  Windows no prolongará ni acortará la vigencia local.
- Al alcanzar los 15 minutos sin una nueva obtención y validación correctas, se
  bloquearán las nuevas navegaciones HTTP/HTTPS y las nuevas descargas.

### Validación y contexto de adquisición

REST/Auth interpretará los campos obligatorios y sus tipos sin suplir indicadores
o listas ausentes con valores por defecto. `PolicyManager` validará los datos ya
interpretados: identificadores positivos, compañía de la instalación, modos
conocidos, reglas y metadatos conformes al contrato existente. Estas comprobaciones
se mantendrán aunque una funcionalidad esté desactivada.

La política se publicará o rechazará completa. Una regla inválida impedirá aplicar
el conjunto; no se publicarán partes ni se completarán campos con la política
anterior.

Cada consulta conservará el contexto concreto de sesión e instalación que la
inició. La correspondencia se comprobará antes de publicar, también si el contexto
cambió durante la validación. Una respuesta de una sesión terminada no podrá
modificar una nueva, aunque ambas utilicen la misma cuenta e instalación.
Sus errores tampoco podrán terminar la nueva sesión o retirar su autorización.

Rechazar o descartar una respuesta no renovará la vigencia ni generará
`POLICY_UPDATED`. Solo podrá conservarse una política anterior que siga vigente
y autorizada en el contexto actual.

### Compartición del estado

La política, el contexto y la vigencia se compartirán como un estado inmutable
completo. Un mutex breve protegerá obtener una referencia o sustituir ese estado;
la validación, evaluación, entrega de eventos y llamadas externas se realizarán
fuera del bloqueo. No se expondrá una publicación parcial.

Conservar una referencia anterior no renovará su vigencia ni sustituirá las
comprobaciones finales de contexto y autorización. El gestor vivirá hasta retirar
los callbacks que puedan utilizarlo, respetando los hilos correspondientes de
Qt y CEF.

### Errores de adquisición y autorización

REST/Auth interpretará los resultados de la consulta de política del dispositivo
actual y comunicará sus estados al motor. `PolicyManager` no interpretará HTTP
ni gestionará JWT o su renovación.

| Resultado | Tratamiento |
| --- | --- |
| Sin conexión, timeout, fallo temporal del servidor o política inválida | Conservar la última política válida solo durante su vigencia restante, sin renovar el plazo. |
| `401` | REST/Auth intentará recuperar la sesión; mientras se resuelve, solo podrá utilizarse la vigencia restante de la política anterior. |
| REST/Auth confirma que la sesión no puede recuperarse | Terminar la sesión y cancelar descargas pendientes y activas. |
| `403`, `404` o `409 DEVICE_INACTIVE` de la consulta de política del dispositivo actual | Retirar inmediatamente la autorización de la instalación, bloquear nuevas navegaciones HTTP/HTTPS y descargas y cancelar descargas pendientes y activas. |

Un `401` por sí solo no confirmará el fin de sesión. Un fallo temporal de conexión
durante la renovación tampoco lo confirmará ni ampliará la vigencia. La denegación
explícita de la instalación no esperará al vencimiento de la política. No se
deducirá de errores de otras operaciones, recursos o contextos anteriores.

## Consecuencias

- El arranque sin política válida no permite operar sin las restricciones del
  dispositivo, pero mantiene disponible el acceso necesario para obtenerlas.
- Un corte de conexión no interrumpe inmediatamente el uso si todavía queda
  vigencia, aunque durante ese plazo pueden aplicarse reglas antiguas.
- La política se conserva únicamente en memoria en este alcance. Este ADR no
  incorpora persistencia de políticas en SQLite.
- Se deberá poder controlar el tiempo en las pruebas sin esperar 15 minutos reales.
- Las respuestas deberán conservar la identidad del contexto solicitante hasta
  su aplicación o descarte, sin reutilizar el estado de una sesión terminada.
- El estado inmutable permitirá lecturas coherentes sin mantener el mutex durante
  evaluación, diálogos, almacenamiento o comunicaciones.
- La denegación explícita retirará la autorización antes del vencimiento local.
  El tratamiento de descargas autorizadas ante caducidad, cambios de política,
  fin de sesión o denegación se concreta en el ADR-0014.

## Validación

La implementación y las pruebas están pendientes. Se comprobará que:

- Sin política válida se bloquean nuevas navegaciones HTTP/HTTPS y descargas,
  con el aviso previsto y el acceso a la interfaz y adquisición de política.
- La política conserva sus reglas antes del límite y caduca al alcanzar los
  15 minutos, utilizando un reloj controlado en las pruebas unitarias.
- Una consulta correcta renueva la vigencia incluso con contenido idéntico.
- Los errores y la desconexión no renuevan la vigencia.
- Cambiar la hora de Windows no modifica el tiempo de validez.
- La política conservada no se reutiliza en otra sesión o instalación.
- Una respuesta incompleta o inválida se rechaza entera, sin publicar partes ni
  generar `POLICY_UPDATED` o renovar la vigencia.
- Respuestas y errores tardíos de una sesión anterior no modifican la actual,
  incluso con la misma cuenta e instalación o un cambio durante la validación.
- Las lecturas concurrentes obtienen un estado completo y las referencias
  anteriores no omiten los controles finales ni renuevan su vigencia.
- El mutex se limita al acceso o sustitución del estado y los callbacks se
  retiran antes de destruir el gestor.
- Un `401` permite intentar recuperar la sesión sin darla por terminada, y un
  fallo temporal durante la renovación no amplía la vigencia.
- La confirmación de sesión irrecuperable y las denegaciones explícitas producen
  el tratamiento acordado, sin confundir errores de otras consultas o contextos.
- La integración con CEF y Qt respeta el bloqueo y mantiene disponibles los
  usos internos necesarios; los tests unitarios no sustituyen esta comprobación.

## Referencias

- [Tarea #114: contratos, límites y decisiones del MVP](https://github.com/CristianPG1993/securebrowser-platform/issues/114).
- [Diseño del Security Engine](../architecture/security-engine.md).
- [Contratos REST](../architecture/rest-api-contracts.md).
- [ADR-0010: contratos REST](0010-rest-api-contracts.md).
- [ADR-0014: autorización y cancelación de descargas](0014-desktop-download-authorization.md).
- [ADR-0015: producción y entrega de eventos](0015-desktop-security-event-production.md).
