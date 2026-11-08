# Security Engine MVP

Documento de diseño para la [tarea #114](https://github.com/CristianPG1993/securebrowser-platform/issues/114).
Los acuerdos de este documento están aprobados por el autor; su implementación está pendiente.

## Componentes y contratos internos

| Componente | Responsabilidad |
| --- | --- |
| `PolicySnapshot` | Copia validada de la política, inmutable una vez publicada. |
| `PolicyManager` | Validar y publicar la política actual, gestionar el contexto de sesión e instalación y comprobar su vigencia local. Sin HTTP ni SQLite. |
| `UrlFilter` | Evaluar un destino ya interpretado con una política y devolver una decisión. Sin dependencias de Qt o CEF. |
| `DownloadPolicy` | Evaluar los datos de descarga con una política. Sin diálogos, acceso al disco ni llamadas CEF. |
| `SecurityDecision` | Representar `ALLOW` o `BLOCK` con un motivo enumerado. |
| Producción de eventos | Generar los eventos al aplicar las operaciones correspondientes y entregarlos al receptor aprobado. |

La adquisición REST y los diálogos permanecerán en sus módulos. `BrowserSession`
realizará la composición, conservando su función de coordinación del navegador
y la interfaz. El núcleo de evaluación y gestión de política podrá comprobarse
con GoogleTest sin arrancar Qt, Chromium, SQLite o el backend.

### Entradas y salidas entre componentes

| Componente o frontera | Entrada | Salida |
| --- | --- | --- |
| REST/Auth → `PolicyManager` | Política completa ya interpretada y resultado de adquisición, asociados al contexto de sesión e instalación solicitante. | Publicación completa, rechazo o descarte por contexto, sin realizar HTTP dentro del gestor. |
| Estado local de `PolicyManager` | Política validada, contexto de autorización y medición de vigencia mediante reloj monotónico. | Referencia a un estado inmutable coherente y estado de disponibilidad para las operaciones protegidas. |
| `UrlFilter` | Política utilizable y datos simples del destino: resultado del análisis, esquema, host ASCII preparado para comparar y clasificación dominio/IPv4/IPv6. | `SecurityDecision`, sin reinterpretar la URL ni producir eventos. |
| `DownloadPolicy` | Política utilizable y nombre de archivo sugerido o definitivo que se vaya a comprobar, separado de la carpeta. | `SecurityDecision` tras validar el nombre y aplicar el control de extensión correspondiente, sin acceder al disco ni abrir diálogos. |
| Aplicación en CEF y Qt | Decisión y contexto de la operación, con la comprobación final de autorización cuando corresponda. | Continuación o cancelación de la operación y aviso de bloqueo según el flujo acordado. |
| Receptor de eventos | `SecurityEvent` fijado al producirse y contexto de sesión e instalación de origen. | Resultado explícito de entrega; los fallos serán observables y no cambiarán un bloqueo. |

Los datos de política conservarán la correspondencia con `PolicyResponse`: `id`,
`companyId`, `name`, indicadores, modos, listas de cadenas, `createdAt` y `updatedAt`.
La sesión, el Device y la vigencia serán contexto local, sin campos nuevos en la
respuesta del backend. Los evaluadores recibirán valores del núcleo, no objetos
Qt, callbacks CEF, respuestas HTTP o conexiones SQLite.

## Resultado de evaluación: `SecurityDecision`

Cada decisión contendrá una acción, `ALLOW` o `BLOCK`, y un motivo enumerado.
El motivo será un valor controlado del motor, no un mensaje libre del backend.

| Grupo | Motivos |
| --- | --- |
| Permitido | Operación permitida. |
| Autorización | Contexto incorrecto o instalación sin autorización. |
| Política | Política no disponible o caducada. |
| Navegación | URL inválida, esquema no admitido, IP bloqueada, dominio en `DENYLIST` o ausente de `ALLOWLIST`. |
| Descarga | Nombre inválido, ausencia de extensión, extensión en `DENYLIST` o ausente de `ALLOWLIST`. |

Para las operaciones protegidas se comprobará, en este orden:

1. Contexto y autorización.
2. Disponibilidad y vigencia de la política.
3. Datos de la operación.
4. Reglas aplicables según su activación y modo.

Se devolverá el primer motivo que impida continuar. Por ejemplo, si la política
está caducada y sus reglas incluían el dominio en una `DENYLIST`, el resultado
será `BLOCK` por política caducada. No se utilizarán esas reglas para justificar
el bloqueo. Si todas las comprobaciones aplicables se superan, se devolverá
`ALLOW` con el motivo de operación permitida.

El orden describe el flujo completo, conservando el reparto de responsabilidades:
el gestor comprobará el estado de política y contexto, y los evaluadores puros
evaluarán los datos y reglas que les correspondan, sin gestionar HTTP o JWT.
Se mantendrán las excepciones internas de CEF y el alcance de operaciones
protegidas ya acordados.

La interfaz traducirá los motivos a mensajes para el usuario. Los eventos de
bloqueo utilizarán sus códigos estables, sin depender del texto mostrado.

## Origen de la política y uso de fixtures

### Política asignada al Device de la instalación

REST/Auth identificará el Device propio de la instalación mediante su
`deviceIdentifier` estable y el contexto autenticado. Una vez resuelto el Device,
consultará `GET /api/v1/devices/{id}/policy` utilizando su identificador del
backend. El `deviceIdentifier` de la instalación y el `id` de la ruta son datos
distintos.

Desktop utilizará exclusivamente la política asignada que devuelva ese endpoint.
No utilizará el catálogo administrativo para que el usuario elija una política
ni aplicará la de otro Device como alternativa. Sin un Device identificado y
autorizado se mantendrá el bloqueo de las operaciones protegidas.

REST/Auth entregará al gestor la política completa ya interpretada y el contexto
que inició la consulta, conforme a las comprobaciones del apartado siguiente.
La obtención real corresponderá a la [tarea #122](https://github.com/CristianPG1993/securebrowser-platform/issues/122)
y su aplicación al motor a la [tarea #123](https://github.com/CristianPG1993/securebrowser-platform/issues/123).

### Preparación de la demostración

La demostración reutilizará el Device ya existente de la demo para su propietario.
La preparación de su identidad de instalación y del flujo autenticado se realizará
en #122. El backend actual no dispone de enrollment HTTP operativo; este acuerdo
no incorpora endpoints nuevos ni implementa el alta de instalaciones.

### Fixtures de pruebas y desarrollo

Se utilizarán políticas de prueba en tests y recorridos de desarrollo explícitos.
Permitirán comprobar los evaluadores y la integración con CEF antes de completar
REST. La aplicación normal no recurrirá a políticas ficticias cuando falle la
consulta real o no se identifique un Device válido.

| Evidencia | Qué permite comprobar |
| --- | --- |
| Pruebas del núcleo con fixtures | Validación de políticas, decisiones, reglas, vigencia y contexto sin Qt, CEF ni backend. |
| Pruebas de integración con CEF y fixtures | Aplicación de los controles en el navegador dentro del recorrido de prueba, sin acreditar adquisición REST. |
| Recorrido con política real | Identificación del Device, obtención autenticada y aplicación de su política mediante #122 y #123. |

Los resultados con fixtures no se presentarán como demostración de política real,
persistencia SQLite o sincronización. Esos bloques requerirán su propia evidencia.

## Validación y contexto de adquisición de la política

REST/Auth interpretará la respuesta completa y comprobará los campos obligatorios
y sus tipos antes de entregarla al motor. Un indicador ausente no se convertirá
automáticamente en `false`, ni una lista ausente se sustituirá por una lista vacía.

`PolicyManager` validará los datos ya interpretados: identificadores positivos,
compañía correspondiente a la instalación, modos conocidos y reglas válidas.
El nombre y las fechas también deberán cumplir el contrato de política existente.
Estas comprobaciones se mantendrán aunque alguna funcionalidad esté desactivada.

La política se aceptará o rechazará como un conjunto. Una regla inválida impedirá
publicar toda la respuesta; no se aplicarán únicamente las reglas válidas ni se
mezclarán campos recibidos con otros de la política anterior para completarla.

### Correspondencia con el contexto solicitante

Cada consulta quedará asociada al contexto concreto de sesión e instalación que
la inició. La respuesta solo podrá aplicarse a ese contexto mientras siga siendo
el actual y válido. No se asociará al contexto que esté activo cuando llegue.

Si terminó o cambió la sesión o la instalación, se descartará la respuesta.
Volver a iniciar sesión con la misma cuenta y utilizar la misma instalación no
convertirá una respuesta de la sesión anterior en una respuesta de la nueva.
Comparar únicamente el usuario o el identificador de dispositivo no bastará
para acreditar la correspondencia con la sesión solicitante.

La correspondencia se comprobará antes de publicar la política, de modo que un
cambio de contexto durante la validación tampoco permita aplicarla. Los resultados
de error de consultas anteriores también conservarán su contexto: no podrán
retirar la autorización ni terminar una sesión nueva por error.

Rechazar una política inválida o descartar una respuesta de otro contexto no
renovará la vigencia ni generará `POLICY_UPDATED`. La política anterior solo
conservará la vigencia y autorización que todavía tenga; no se recuperará una
política de una sesión terminada.

Por ejemplo, si una consulta empieza en la sesión A, esta termina y se inicia
la sesión B, la respuesta tardía de A no modificará el estado de B, aunque ambas
sesiones correspondan al mismo usuario e instalación.

## Compartición de la política y vida de los objetos

Se compartirá un estado inmutable con la política, el contexto de sesión e
instalación y la vigencia local como una unidad coherente. Estos datos locales
de contexto y vigencia no se añadirán al contrato REST del backend.

Un mutex breve protegerá únicamente la obtención de una referencia al estado
actual o su sustitución completa. La validación se realizará antes de publicar,
fuera del mutex; no se expondrá un estado parcialmente validado o actualizado.

Cada evaluación conservará una referencia al estado inmutable obtenido. Publicar
una actualización no modificará los datos de una evaluación que ya tenga esa
referencia. Conservarla no renovará la vigencia ni sustituirá las comprobaciones
de sesión y autorización, incluida la comprobación final antes de `Continue`.

La evaluación, la entrega de eventos y las llamadas Qt o CEF se realizarán fuera
del mutex. No se mantendrá el bloqueo durante operaciones de red, SQLite,
diálogos o esperas de respuestas entre hilos.

La vida de `PolicyManager` se mantendrá hasta retirar los callbacks que puedan
utilizarlo. La retirada de conexiones y referencias formará parte de la
coordinación de cierre existente, sin acceder a objetos destruidos.

Los widgets seguirán utilizándose exclusivamente desde el hilo principal de Qt.
Los callbacks y operaciones CEF respetarán sus hilos y la comunicación explícita
ya utilizada por Desktop Foundation.

## Activación y evaluación de reglas

Las nuevas navegaciones web y descargas requieren una política válida y vigente
para la sesión e instalación. Desactivar una funcionalidad no elimina ese
requisito ni permite utilizar una política malformada; los modos y listas
seguirán siendo obligatorios y válidos conforme al contrato del backend.

Las URL HTTP/HTTPS que no puedan interpretarse de forma válida se bloquearán,
también con URL Filtering desactivado. Se mantendrán los límites de esquemas
y las excepciones internas definidos en este documento.

### Activación de URL Filtering

- Con `urlFilteringEnabled = true`, se aplicará la restricción de IP y se
  evaluarán los dominios mediante el modo y las reglas de la política.
- Con `urlFilteringEnabled = false`, se omitirán las reglas de dominio y la
  restricción específica de IP, conservando las comprobaciones generales anteriores.
- Una regla de dominio coincidirá con el dominio exacto o sus subdominios,
  respetando el límite de etiqueta: `example.com` coincidirá con
  `www.example.com`, pero no con `myexample.com` ni `example.com.evil.com`.

### Activación de Download Control

- Con `downloadControlEnabled = true`, se evaluará la última extensión del
  nombre, normalizada en minúsculas y sin punto. Un archivo sin extensión
  se bloqueará antes de comprobar la pertenencia a las listas.
- Con `downloadControlEnabled = false`, se omitirán las listas de extensiones
  y el bloqueo por ausencia de extensión. Un archivo como `informe` podrá
  descargarse si supera las demás comprobaciones de autorización y guardado.
- Se mantendrán las comprobaciones necesarias para guardar un archivo válido
  en el destino elegido, con independencia del indicador de activación.

Este acuerdo concreta el requisito inicial de archivos sin extensión: su
bloqueo por Download Control se aplica únicamente con la funcionalidad activada.
Por ejemplo, `documento.pdf.exe` se evaluará por `exe` cuando el control esté activo.

### Modos y listas vacías

Con la funcionalidad activada, después de superar sus comprobaciones previas,
la pertenencia a las reglas determinará el resultado:

| Modo | Coincide con una regla | No coincide con ninguna regla |
| --- | --- | --- |
| `DENYLIST` | `BLOCK` | `ALLOW` |
| `ALLOWLIST` | `ALLOW` | `BLOCK` |

Una `DENYLIST` vacía no bloqueará por pertenencia a la lista. Una `ALLOWLIST`
vacía bloqueará todos los elementos sometidos al control. Desactivar la
funcionalidad omitirá su modo y listas, aunque estén configurados como `ALLOWLIST`
vacía; no anulará los requisitos generales de política válida y autorización.

## Normalización para comparar reglas

### Dominios

Se comparará el host obtenido por `CefParseURL` en su representación ASCII,
en minúsculas y sin un punto final. Por ejemplo, el host de
`https://EXAMPLE.COM./` se comparará como `example.com`. Esta preparación no
sustituirá la interpretación de la URL ni la clasificación de las IP.

Los dominios internacionales se compararán mediante su representación ASCII
con la regla normalizada recibida del backend. Por ejemplo, `bücher.de` se
comparará mediante `xn--bcher-kva.de`. No se reconvertirá la regla ASCII recibida
para introducir otras equivalencias.

Java y Chromium pueden convertir algunos dominios internacionales de forma
diferente. En el MVP se respetará el valor que realmente devuelve el backend:
si no coincide con el host interpretado ni con una regla de dominio padre,
se aplicará el resultado de no coincidencia del modo configurado. No se añadirán
alias silenciosos para intentar compensar esas diferencias.

Esta limitación se documentará y se comprobará con casos de integración de la
versión fijada de CEF. El acuerdo no modifica la normalización del backend ni
afirma que las conversiones de ambas plataformas sean equivalentes. Las
referencias son la [documentación de IDN de Java](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/net/IDN.html)
y el [código de conversión IDN de Chromium](https://raw.githubusercontent.com/chromium/chromium/main/url/url_idna_icu.cc).

### Extensiones y caracteres Unicode

La última extensión se comparará en minúsculas Unicode y sin el punto separador,
con una conversión independiente del idioma de Windows y compatible con
`String.toLowerCase(Locale.ROOT)`, utilizado por el backend. Por ejemplo,
`informe.Ñ` se evaluará como `ñ`, conservando el nombre válido del archivo.

No se eliminarán acentos ni se considerarán equivalentes caracteres distintos
por su parecido visual. Tampoco se limitarán las extensiones a ASCII.
La compatibilidad de la conversión se comprobará antes de dar por válida su
implementación, incluidos casos Unicode; no bastará con comprobar `PDF` y `EXE`.
La referencia es la [documentación de conversión a minúsculas de Java](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/lang/String.html#toLowerCase(java.util.Locale)).

## Política no disponible

Decisión aprobada el 8 de noviembre de 2026.

Al arrancar, si Desktop no dispone de una política válida asignada a su
dispositivo, bloqueará las nuevas navegaciones HTTP/HTTPS y las descargas.
Este comportamiento se denomina *fail closed*: sin la configuración necesaria
para evaluar una operación protegida, no se autoriza esa operación.

La interfaz, el inicio de sesión y la comunicación REST necesaria para obtener
la política permanecerán disponibles. Se preservarán los usos internos de CEF
necesarios para mostrar la aplicación.

La interfaz mostrará el mensaje «Política de seguridad no disponible».

## Desconexión del backend

Si el backend se desconecta cuando Desktop ya dispone de una política válida,
se conservará esa política en memoria durante un tiempo limitado, siempre que
corresponda a la misma sesión e instalación.

Un fallo de conexión no eliminará la política ni reiniciará su plazo de vigencia.
Mientras ese plazo no se agote, se seguirán aplicando sus reglas.
Al agotarse, se bloquearán las nuevas navegaciones HTTP/HTTPS y las nuevas descargas.

Este acuerdo no incorpora almacenamiento de políticas en SQLite; al arrancar
sin una política válida se mantiene el bloqueo definido en el apartado anterior.

## Errores de adquisición y autorización

REST/Auth interpretará el resultado de la consulta de política del dispositivo
actual y comunicará al motor el estado correspondiente a la sesión e instalación.
`PolicyManager` gestionará ese estado sin interpretar códigos HTTP ni gestionar
tokens JWT o su renovación.

| Resultado de la consulta | Comportamiento |
| --- | --- |
| Sin conexión, timeout o fallo temporal del servidor | Mantener la última política válida hasta su vencimiento, sin renovar el plazo. |
| Respuesta con una política inválida | Rechazarla y conservar la última política válida solo durante su vigencia restante. |
| `401` durante la consulta | REST/Auth intentará renovar la sesión; mientras se resuelve, solo podrá utilizarse la política anterior dentro de su vigencia restante. |
| REST/Auth confirma que la sesión no puede recuperarse | Terminar la sesión y cancelar las descargas pendientes y activas. |
| `403` o `404` de la consulta de política del dispositivo actual | Retirar inmediatamente la autorización de esa instalación y dejar de utilizar su política para autorizar operaciones. |
| `409 DEVICE_INACTIVE` de esa consulta | Retirar inmediatamente la autorización de la instalación, igual que en el caso anterior. |

Un `401` por sí solo no confirmará el fin de la sesión. Si la renovación falla
por un problema temporal de conexión, se conservará únicamente la vigencia
restante de la política, sin iniciar otro plazo de 15 minutos. Si REST/Auth
confirma que la sesión no puede recuperarse, se aplicará su cierre.

Una denegación explícita de la instalación bloqueará las nuevas navegaciones
HTTP/HTTPS y las nuevas descargas, y cancelará las descargas pendientes y activas.
No se esperará a que venza la política conservada en memoria. Esta cancelación
se añade a las ya previstas por fin de sesión o cierre del navegador.

La interpretación de `403`, `404` y `DEVICE_INACTIVE` de este apartado se limita
a la consulta de política del dispositivo actual. No se deducirá la retirada
de autorización a partir de errores de otras operaciones o recursos.

## Vigencia local de la política

La política tendrá una vigencia local de 15 minutos desde su última obtención
y validación correctas para la misma sesión e instalación.

Una consulta correcta renovará ese plazo, aunque la política recibida no haya
cambiado. Un fallo de conexión o una respuesta inválida no renovarán la vigencia.
La desconexión tampoco iniciará un nuevo plazo de 15 minutos.

El tiempo transcurrido se medirá con un reloj monotónico, de modo que los cambios
en la hora de Windows no prolonguen ni acorten la vigencia local.
Al alcanzar los 15 minutos sin otra obtención y validación correctas, se
bloquearán las nuevas navegaciones HTTP/HTTPS y las nuevas descargas.

Por ejemplo, una política validada a las 12:00 y una desconexión a las 12:10
supondrán el bloqueo a las 12:15 si no se obtiene otra política válida.
Estas horas ilustran el plazo; su medición utilizará el reloj monotónico.

## Navegación a direcciones IP

Con `urlFilteringEnabled = true`, se bloquearán las navegaciones HTTP/HTTPS
cuyo host sea un literal IPv4 o IPv6, tanto en modo `DENYLIST` como `ALLOWLIST`.
Esta restricción se aplicará aunque la dirección IP aparezca en las reglas.

Con el filtrado de URL desactivado, no se aplicará esta restricción específica.
Se mantiene el requisito de disponer de una política válida y vigente.

Una dirección IP no se comparará utilizando coincidencia por subdominios.
Su identificación se realizará sobre el host interpretado de la URL, no
buscando cifras o puntos en el texto completo.

La restricción afecta a las navegaciones CEF sometidas a URL Filtering y no
impide que el cliente REST contacte con el backend local.

Los destinos expresados mediante un dominio seguirán evaluándose por ese
nombre, aunque su conexión utilice una dirección IP.

## Interpretación y evaluación de URL

El adaptador CEF utilizará `CefParseURL` del SDK fijado para interpretar la URL
y obtener sus componentes, incluidos el esquema y el host.
El adaptador clasificará el host como dominio, IPv4 o IPv6; `CefParseURL` no
proporciona directamente esa clasificación.

`UrlFilter` recibirá datos simples ya interpretados, incluido el resultado del
análisis, y devolverá una decisión con su motivo. No dependerá de Qt ni de CEF
ni volverá a interpretar la URL mediante otro parser.

`QUrl` permanecerá en la barra de direcciones como ayuda para la entrada del
usuario. Su validación no sustituirá el control de seguridad en CEF.

Esta separación permitirá probar el evaluador con GoogleTest sin arrancar Qt
ni Chromium. La interpretación real de URL y la clasificación del host se
comprobarán por separado en las pruebas del adaptador con CEF.

## Alcance del filtrado de navegación

URL Filtering cubrirá las navegaciones HTTP/HTTPS del documento principal y
de los subframes del único navegador CEF, incluidos los documentos cargados
dentro de un `iframe`.

Se aplicará a las navegaciones iniciadas desde la barra de direcciones, enlaces,
formularios, JavaScript, Atrás, Adelante y Recargar, así como a las redirecciones
de las navegaciones cubiertas. Los popups seguirán cancelados, conforme al
comportamiento existente de Desktop Foundation.

Quedarán fuera del filtrado de dominios las solicitudes de imágenes, scripts,
CSS, `fetch`/XHR, WebSocket y las solicitudes autónomas de service workers.
Las navegaciones cubiertas que sean atendidas por un service worker deberán
verificarse igualmente; no quedan exentas por ese motivo.

Las nuevas navegaciones cubiertas servidas desde caché o iniciadas mediante
historial seguirán sometidas al control. Las pruebas de integración comprobarán
estos recorridos después de cambiar la política, además de las cargas de red.

Este alcance corresponde a un filtro de navegación. Bloquear un dominio como
destino de navegación no garantiza impedir todas las solicitudes de red hacia
él: una página permitida podría solicitar recursos a ese dominio mediante
los mecanismos excluidos.

## Esquemas admitidos en el MVP

El MVP se limitará a navegación web HTTP/HTTPS y a las excepciones internas
necesarias para el funcionamiento de CEF.

| Destino de navegación | Tratamiento |
| --- | --- |
| `http:` y `https:` | Validar la URL y aplicar la política correspondiente. |
| `about:blank` y documentos `about:srcdoc` de subframes | Preservar su funcionamiento; las navegaciones web que originen seguirán pasando por el control. |
| Otros usos internos necesarios de CEF | Admitir únicamente casos identificados y comprobados durante la integración, sin autorizar globalmente `chrome:` o `devtools:`. |
| `file:`, `data:`, `blob:` y `ftp:` como destinos de navegación | Bloquearlos en el MVP. |
| Protocolos externos, como `mailto:` o `tel:`, y esquemas desconocidos | No autorizar esas navegaciones ni abrir aplicaciones externas. |

Este límite se aplicará al documento principal y a los subframes incluso con
`urlFilteringEnabled = false`. Desactivar el filtrado de dominios e IP no
ampliará los protocolos admitidos.

La tabla regula destinos de navegación. No añade un bloqueo general de recursos
`data:` o `blob:` ni desactiva el JavaScript normal de las páginas.
Las descargas se tratarán por separado mediante Download Control.

## Selección y evaluación del nombre de descarga

Se utilizará un diálogo estándar de Qt de «Guardar como», con ubicación y
nombre editables. La selección del destino se realizará antes de autorizar
la continuación de la descarga en CEF.

El flujo será el siguiente:

1. Validar y evaluar el nombre sugerido por CEF. Si está bloqueado, cancelar la descarga
   y mostrar el motivo sin abrir el diálogo de guardado.
2. Si está permitido, mostrar «Guardar como» para elegir la ubicación y el nombre.
3. Si el usuario confirma el diálogo, comprobar la política local vigente y
   validar y evaluar el nombre definitivo elegido con esa política.
4. Continuar únicamente si esa evaluación permite guardarlo, proporcionando
   la ruta explícita mediante `Continue(ruta, false)`.

Si el nombre definitivo está bloqueado, se cancelará la descarga y se mostrará
el motivo. No se delegará después la elección del nombre a otro diálogo de CEF.

La cancelación voluntaria de «Guardar como» terminará la descarga sin
registrarse como un bloqueo de política ni generar `DOWNLOAD_BLOCKED`.

Por ejemplo, permitir el nombre sugerido `documento.pdf` no permitirá guardar
`documento.exe` si su extensión está bloqueada por la política.

### Validez del nombre y extracción de extensión

Se validará el nombre antes de aplicar las reglas de extensión. Los nombres
inválidos se rechazarán sin corregirlos silenciosamente: no se eliminarán
automáticamente puntos o espacios finales para convertirlos en otros nombres.

Se rechazarán nombres vacíos, `.` y `..`, nombres terminados en punto o espacio,
caracteres de control y caracteres reservados de Windows, incluidos `:`, los
separadores de ruta y los demás caracteres no admitidos en un nombre.
También se rechazarán los nombres reservados de dispositivos de Windows,
incluso acompañados de extensión, como `NUL.txt`.

La carpeta seleccionada y el nombre de archivo se tratarán por separado: las
restricciones de caracteres del nombre no se aplicarán indiscriminadamente a
toda la ruta. Los nombres Unicode válidos se conservarán, sin limitarlos a ASCII.

| Nombre | Tratamiento |
| --- | --- |
| `informe.PDF` | Conservar el nombre y evaluar la extensión como `pdf`. |
| `informe.pdf.exe` | Evaluar la última extensión: `exe`. |
| `informe` o `.env` | Considerarlos sin extensión y bloquearlos por ese motivo solo con Download Control activo. |
| `informe.` o un nombre terminado en espacio | Rechazar el nombre. |
| `NUL.txt` y otros nombres reservados de Windows | Rechazarlos. |
| `informe.pdf:datos` | Rechazarlo por contener `:` en el nombre. |

Un nombre inválido se rechazará aunque Download Control esté desactivado,
porque se trata de una comprobación de guardado. Un nombre válido sin extensión
podrá permitirse con ese control desactivado, manteniendo los demás requisitos
de autorización y guardado.

La referencia para las restricciones de nombres es la
[documentación de Microsoft sobre nombres de archivos y rutas](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).

### Política al confirmar «Guardar como»

Justo antes de llamar a `Continue`, se comprobará que existe una política local
válida y vigente para la misma sesión e instalación que inició la solicitud.
El nombre definitivo se evaluará con la política aplicada en ese momento.

Si la política cambió mientras el diálogo estaba abierto, se aplicarán las
reglas nuevas. Si caducó, no existe una política válida o terminó la sesión,
se cancelará la descarga y se mostrará el motivo. La solicitud no se trasladará
a otra sesión o instalación.

Se utilizará el snapshot local vigente, sin exigir una consulta REST por cada
confirmación. Esta decisión se limita a las descargas todavía pendientes de
autorización.

Por ejemplo, si se abrió el diálogo para guardar `informe.pdf` y después se
aplicó una política que bloquea `pdf`, al confirmar se bloqueará la descarga.

### Descargas autorizadas y en curso

Una descarga autorizada mediante `Continue` y todavía en curso podrá terminar
durante la misma sesión e instalación mientras sigan autorizadas, aunque después
cambie o caduque la política. En el MVP no se reevaluarán esas transferencias
por dichos cambios.

Las nuevas solicitudes y las descargas pendientes de confirmar «Guardar como»
seguirán sujetas a la política local vigente antes de autorizar su continuación.
Al terminar la sesión, cerrar el navegador o recibir una denegación explícita
de la instalación conforme al apartado de errores de adquisición y autorización,
se cancelarán las descargas pendientes y activas.

Por ejemplo, si un PDF empieza a descargarse estando permitido y después se
aplica una política que bloquea `pdf`, esa descarga podrá terminar, pero las
siguientes solicitudes quedarán bloqueadas por la nueva política.

## Producción y entrega de eventos

Los eventos se entregarán a un receptor pequeño proporcionado al motor mediante
inyección. El Security Engine no mantendrá una cola interna de eventos pendientes.
La persistencia en SQLite y la sincronización HTTP corresponderán a sus bloques
respectivos, separados de la evaluación y aplicación de reglas.

Los evaluadores puros devolverán decisiones con su motivo, sin generar eventos
por cada evaluación. Al aplicar efectivamente un bloqueo, se producirá el evento
correspondiente y se entregará al receptor.

El componente que aplique correctamente una actualización de política producirá
`POLICY_UPDATED` conforme a la semántica definida en su apartado correspondiente.

El receptor deberá comunicar los fallos de entrega de forma observable.
Si la entrega falla, el bloqueo se mantendrá y no se afirmará que el evento esté
persistido o sincronizado.

En GoogleTest se utilizará un receptor de prueba que capture los eventos para
comprobar su contenido sin arrancar Qt, Chromium, SQLite ni el backend.
Esta captura de prueba no acredita almacenamiento durable ni envío real.

### Datos de `SecurityEvent` y contexto de origen

El evento conservará los cuatro datos compatibles con el contrato del backend:

| Campo | Contenido |
| --- | --- |
| `eventUuid` | UUID generado una sola vez al producir el evento. |
| `type` | `URL_BLOCKED`, `DOWNLOAD_BLOCKED` o `POLICY_UPDATED`. |
| `occurredAt` | Instante del suceso en UTC, con precisión de microsegundos. |
| `details` | Cadena con el detalle estructurado y el contexto mínimo del suceso. |

`occurredAt` utilizará la hora del sistema y se representará en UTC al enviarlo.
La vigencia de políticas seguirá midiéndose con el reloj monotónico; la fecha
del evento no sustituirá esa medición.

El evento quedará fijado al producirse. Los reenvíos conservarán el UUID, tipo,
fecha y contenido de `details`, sin regenerarlos a partir de la política o la
hora que estén vigentes al reenviar.

El receptor recibirá también el contexto de sesión e instalación de origen,
separado de los cuatro campos del evento enviados por REST. Ese contexto permitirá
asociarlo a la instalación que produjo el suceso, sin utilizar automáticamente
la sesión que esté activa después. El motor no añadirá campos de propietario
al cuerpo REST ni asignará `receivedAt`, que corresponde al backend.

El contrato de transporte existente se recoge en los
[contratos de la API REST](rest-api-contracts.md).

### Cuándo producir `POLICY_UPDATED`

El evento representará un cambio de política aplicado correctamente:

- La primera política válida de cada sesión no generará `POLICY_UPDATED`.
- Sustituir una política anterior por otra diferente generará el evento después
  de aplicar correctamente la sustitución.
- Consultar de nuevo la misma política para renovar su vigencia no generará
  el evento.
- Una política inválida, rechazada o cuya aplicación falle no generará el evento.

Por ejemplo, obtener y aplicar la política al iniciar sesión no producirá este
evento. Recibir y aplicar después un cambio de reglas realizado por el
administrador sí lo producirá.

### Comparación de políticas para `POLICY_UPDATED`

Se compararán la identidad y la configuración de seguridad de políticas
previamente validadas:

| Campos | ¿Su cambio genera el evento después de aplicarse correctamente? |
| --- | --- |
| `id` | Sí, aunque la configuración sea idéntica. |
| `urlFilteringEnabled`, `urlFilteringMode`, `urlRules` | Sí. |
| `downloadControlEnabled`, `downloadControlMode`, `downloadRules` | Sí. |
| `name`, `createdAt`, `updatedAt` | No, si únicamente cambian estos metadatos. |

Las reglas se compararán normalizadas, sin que cambiar su orden cuente como
modificación. `companyId` se validará contra el contexto de la instalación antes
de comparar: una política de otra compañía se rechazará.

Los metadatos se conservarán y actualizarán en el snapshot local aunque por sí
solos no provoquen `POLICY_UPDATED`. Un `updatedAt` diferente no se utilizará
como prueba de un cambio de configuración.

La consulta y validación correctas renovarán la vigencia local de 15 minutos
aunque `updatedAt` no cambie. Esa renovación no generará el evento si la
identidad y la configuración de seguridad permanecen iguales.

Por ejemplo, renombrar «Política oficina» actualizará los metadatos sin generar
el evento; añadir un dominio bloqueado sí lo generará después de aplicar el cambio.

### Un evento por operación bloqueada

Las notificaciones correspondientes al mismo bloqueo de una operación producirán
un único evento. Los intentos distintos de navegación o descarga producirán
eventos distintos, aunque coincidan la URL o el nombre de archivo.

Cada evento recibirá su `eventUuid` una sola vez al producirse. Si posteriormente
se reenvía, se conservarán ese UUID y el contenido del evento; su almacenamiento
y reenvío seguirán correspondiendo al bloque Offline.

La URL o el nombre de archivo no se utilizarán como única identidad de la
operación, ni se agruparán intentos distintos únicamente por coincidir dentro
de un intervalo de tiempo. El mecanismo concreto de correlación de las
notificaciones se definirá al integrar los callbacks de CEF.

Por ejemplo, varias notificaciones de una misma cancelación de descarga
producirán un solo `DOWNLOAD_BLOCKED`. Un nuevo intento bloqueado del usuario
producirá otro evento con un UUID diferente.

### Contenido mínimo de `details`

Se utilizará el campo `details` existente, sin modificar el contrato del backend.
Su contenido incluirá un motivo controlado, el identificador de política cuando
esté disponible y el contexto mínimo correspondiente al evento:

| Evento | Contexto |
| --- | --- |
| `URL_BLOCKED` | Host interpretado, como dominio o IP, sin la URL completa. |
| `DOWNLOAD_BLOCKED` | Extensión evaluada, sin el nombre completo ni la ruta del archivo. |
| `POLICY_UPDATED` | Identificación de la política aplicada, sin copiar sus reglas completas. |

No se incluirán credenciales, rutas ni parámetros de la URL, ni nombres completos
o rutas personales de archivos. Si la URL no puede interpretarse, se conservará
el motivo sin utilizar el texto original como alternativa. Para un archivo sin
extensión, se describirá el motivo de bloqueo sin recurrir al nombre completo.

El contenido respetará el tipo cadena y el límite de 2.000 puntos de código
Unicode que valida el backend. No se inventará un identificador de política
cuando ese dato no esté disponible.

Por ejemplo, para `https://example.com/ruta?token=...` se conservarán el host
`example.com` y el motivo, sin la ruta ni los parámetros.

### Formato de `details`

Se utilizará JSON compacto serializado dentro de la cadena `details`, con
`schemaVersion` igual a `1` para identificar el formato. El campo `reason`
contendrá el código estable del motivo; `policyId`, `host` o `extension`
recogerán únicamente los datos disponibles y pertinentes para el tipo de evento,
conforme a las restricciones de contenido anteriores.

Por ejemplo, el contenido de la cadena para un dominio bloqueado podrá ser:

```json
{"schemaVersion":1,"reason":"DOMAIN_DENIED","policyId":7,"host":"example.com"}
```

En la petición HTTP, `details` seguirá siendo una cadena con ese JSON
correctamente escapado, no un objeto JSON del cuerpo de la API. El backend
conservará su contrato actual de cadena; el formato permitirá interpretar
el detalle sin analizar mensajes destinados al usuario.

El contenido serializado se fijará al producir el evento y se conservará en
los reenvíos, manteniendo el límite de 2.000 puntos de código Unicode.

## Configuración de compilación y pruebas

Se mantendrá un único proyecto CMake, con entrada en `desktop/CMakeLists.txt`,
que permitirá configurar la aplicación y las pruebas del núcleo por separado:

| Configuración | `SBP_BUILD_DESKTOP_APP` | `BUILD_TESTING` | Carpeta de salida de referencia |
| --- | --- | --- | --- |
| Habitual, por defecto | `ON` | `OFF` | `desktop/build/app` |
| Pruebas aisladas del núcleo | `OFF` | `ON` | `desktop/build/tests` |

Cada configuración tendrá su propio comando de configuración y su directorio
de construcción, evitando compartir la caché de CMake entre ambas. Las carpetas
indicadas son referencias para el flujo previsto; esta decisión no traslada las
salidas actuales de Desktop Foundation.

Con `SBP_BUILD_DESKTOP_APP = OFF`, CMake no buscará Qt ni CEF. La configuración
de pruebas permitirá compilar el núcleo y sus pruebas GoogleTest sin exigir
esos SDK. CTest ejecutará las pruebas de esa configuración después de compilarlas.

La configuración habitual construirá la aplicación con Qt y CEF y mantendrá las
comprobaciones de Desktop Foundation. Las pruebas del adaptador y la integración
con CEF requerirán su entorno correspondiente; las pruebas aisladas del núcleo no
acreditarán el funcionamiento de los callbacks o la interfaz.

El cambio de CMake, la incorporación de GoogleTest y los comandos definitivos
se implementarán paso a paso en la [tarea #115](https://github.com/CristianPG1993/securebrowser-platform/issues/115).
Este apartado documenta el contrato aprobado; las opciones aún no están implementadas.

## Validación prevista

Para la configuración de compilación y pruebas, se comprobará que:

- Los valores predeterminados construyen la aplicación con Qt y CEF sin exigir
  GoogleTest para ese flujo.
- La configuración con aplicación desactivada y pruebas activadas no busca
  Qt ni CEF y permite compilar el núcleo y sus pruebas sin esos SDK.
- Las dos configuraciones conservan cachés y salidas de construcción separadas.
- CTest descubre y ejecuta las pruebas GoogleTest del núcleo ya compiladas.
- La construcción habitual y las comprobaciones de Desktop Foundation siguen
  funcionando; la evidencia de integración con Qt y CEF se obtiene por separado.

Para los contratos internos y la compartición del estado, se comprobará que:

- La evaluación y la gestión de política pueden probarse sin Qt, CEF, SQLite
  ni red, y las decisiones incluyen `ALLOW` o `BLOCK` y un motivo enumerado.
- Las lecturas concurrentes con una publicación obtienen un estado completo,
  sin mezclar la política de una publicación con el contexto o vigencia de otra.
- Una referencia al estado anterior conserva datos inmutables después de
  publicar otro estado, sin renovar su vigencia ni omitir los controles finales.
- La validación, evaluación, entrega de eventos y llamadas Qt o CEF quedan
  fuera de la sección protegida; el mutex se limita al acceso o sustitución
  del estado compartido.
- La integración retira los callbacks antes de destruir el gestor y conserva
  el uso de widgets en Qt y de operaciones CEF en sus hilos correspondientes.

Para `SecurityDecision` y el orden de comprobación, se comprobará que:

- Cada resultado contiene una acción y un motivo enumerado coherente con ella.
- Superar todas las comprobaciones aplicables devuelve `ALLOW` con el motivo
  de operación permitida.
- Un contexto incorrecto o una instalación sin autorización impiden continuar
  antes de utilizar la política o evaluar las reglas.
- Una política ausente o caducada impide utilizar sus reglas para justificar
  una decisión, aunque la operación también hubiera sido bloqueada por ellas.
- Los datos inválidos de una operación se rechazan antes de evaluar las listas.
- La decisión conserva el primer motivo de bloqueo según las etapas acordadas.
- Cambiar el texto de la interfaz no modifica el motivo ni el código utilizado
  en los eventos de bloqueo.

Para la validación y el contexto de adquisición, se comprobará que:

- REST/Auth rechaza respuestas incompletas o con tipos incorrectos sin suplir
  indicadores o listas ausentes con valores por defecto.
- El gestor rechaza identificadores no positivos, compañías ajenas, modos
  desconocidos, reglas inválidas o metadatos que incumplan el contrato existente.
- Una sola regla inválida impide publicar el conjunto, sin aplicar parcialmente
  la respuesta ni completar campos con los de la política anterior.
- La respuesta solo se aplica a la sesión e instalación que iniciaron la consulta.
- Terminar o cambiar ese contexto antes de recibir la respuesta impide aplicarla,
  incluso después de iniciar otra sesión con la misma cuenta e instalación.
- Un cambio de contexto durante la validación impide publicar la respuesta.
- Un error tardío de una consulta anterior no termina la sesión actual ni retira
  la autorización de su instalación.
- Rechazar o descartar una respuesta no renueva la vigencia ni genera
  `POLICY_UPDATED`, y no recupera políticas de sesiones terminadas.
- Una respuesta rechazada solo permite conservar la política anterior si esta
  sigue vigente y autorizada en el contexto actual.

Para el origen de la política y el uso de fixtures, se comprobará que:

- REST/Auth resuelve el Device propio mediante el `deviceIdentifier` de la
  instalación y consulta su política utilizando el `id` del backend.
- La aplicación utiliza la política asignada a ese Device, sin selección del
  catálogo administrativo ni sustitución por la de otra instalación.
- Sin un Device identificado y autorizado no se habilitan operaciones protegidas.
- La demostración reutiliza el Device existente para su propietario y su
  preparación queda en #122, sin introducir enrollment HTTP nuevo.
- Las fixtures permiten comprobar los evaluadores y la integración CEF en sus
  recorridos explícitos de prueba y desarrollo, antes de completar REST.
- Un fallo de adquisición en la aplicación normal no activa una política ficticia.
- La evidencia con fixtures se distingue de la obtención y aplicación de política
  real y de la persistencia y sincronización de eventos.

Para la activación y evaluación de reglas, se comprobará que:

- Una política ausente, caducada o no utilizable no permite nuevas navegaciones
  web ni descargas por tener desactivadas las funcionalidades.
- Las URL HTTP/HTTPS no evaluables se bloquean también con URL Filtering
  desactivado, y se conserva el límite de esquemas acordado.
- Con URL Filtering desactivado no se bloquea por reglas de dominio ni por la
  restricción específica de IP; con el filtrado activo se aplica el criterio
  de dominio exacto o subdominio, sin coincidencias por subcadenas engañosas.
- Los modos producen los resultados de la tabla y las listas vacías respetan
  sus reglas, tanto en URL Filtering como en Download Control.
- Un archivo sin extensión se bloquea con Download Control activo, incluso
  con `DENYLIST` vacía, y no se bloquea por esa ausencia con el control desactivado.
- Desactivar Download Control omite sus listas, incluida una `ALLOWLIST` vacía,
  sin omitir los demás requisitos de autorización y guardado.
- Se utiliza la última extensión y se conserva la normalización en minúsculas
  y sin punto prevista por el contrato.

Para la normalización y compatibilidad con el backend, se comprobará que:

- El host de `https://EXAMPLE.COM./` se compara como `example.com`, sin omitir
  la interpretación de la URL ni la clasificación de las IP.
- El caso `bücher.de` se compara mediante `xn--bcher-kva.de` con la regla
  ASCII recibida, conservando el criterio de dominio exacto o subdominio.
- Los casos IDN de integración registran el valor devuelto por el backend y
  el host obtenido por la versión fijada de CEF, incluidas sus diferencias.
- Una diferencia entre esas representaciones no introduce alias: si ninguna
  regla coincide, se aplica el resultado previsto por `DENYLIST` o `ALLOWLIST`.
- `informe.Ñ` se evalúa como `ñ`, y la conversión de extensiones Unicode es
  compatible con el backend e independiente del idioma de Windows.
- No se eliminan acentos ni se equiparan caracteres por su parecido visual.
- Las pruebas unitarias del evaluador se distinguen de la evidencia de
  interpretación y compatibilidad obtenida mediante la integración con CEF.

Con Desktop arrancado sin una política válida, se comprobará que:

- Las nuevas navegaciones HTTP/HTTPS se bloquean.
- Las descargas se bloquean.
- La interfaz muestra el motivo y permite iniciar sesión y obtener la política.
- Los usos internos necesarios de CEF siguen funcionando.

Ante una desconexión con una política previamente válida, se comprobará que:

- Sus reglas siguen aplicándose durante el plazo de vigencia.
- Los fallos de conexión no eliminan la política ni reinician ese plazo.
- Al agotarse el plazo, se bloquean las nuevas navegaciones HTTP/HTTPS y las nuevas descargas.
- La política conservada no se reutiliza en otra sesión o instalación.

Para los errores de adquisición y autorización, se comprobará que:

- Los fallos temporales y las políticas inválidas conservan únicamente la
  vigencia restante de la última política válida, sin renovar el plazo.
- Un `401` permite a REST/Auth intentar recuperar la sesión sin darla por
  terminada únicamente por esa respuesta; la espera no amplía la vigencia.
- Un fallo temporal durante la renovación no se interpreta como confirmación
  de sesión irrecuperable, ni permite utilizar una política ya caducada.
- La confirmación de sesión irrecuperable termina la sesión y cancela las
  descargas pendientes y activas.
- Un `403`, `404` o `409 DEVICE_INACTIVE` de la consulta de política del
  dispositivo actual retira inmediatamente su autorización, aunque la política
  todavía no haya caducado, bloquea nuevas operaciones protegidas y cancela
  las descargas pendientes y activas.
- Los errores de otras operaciones o recursos no se interpretan como una
  denegación de la instalación por compartir esos códigos HTTP.
- REST/Auth comunica los estados al motor; `PolicyManager` no necesita
  interpretar HTTP ni gestionar JWT o su renovación.

Para la vigencia local, se comprobará que:

- La política sigue vigente antes de completar los 15 minutos y caduca al alcanzarlos.
- Una consulta correcta renueva el plazo, aunque la política no haya cambiado.
- Las respuestas inválidas y los fallos de conexión no renuevan el plazo.
- La desconexión no inicia un plazo adicional.
- Los cambios en la hora de Windows no modifican la vigencia local.

Para la navegación a direcciones IP, se comprobará que:

- Las navegaciones a literales IPv4 e IPv6 se bloquean con el filtrado activo
  en ambos modos, incluso si la IP aparece en las reglas.
- Con una política válida y vigente y el filtrado desactivado, no se produce
  un bloqueo por esta restricción específica de IP.
- Las variantes de escritura que el parser interprete como IP reciben el
  mismo tratamiento y no eluden el bloqueo.
- Una dirección IP no se compara como si fuera un dominio con subdominios.
- Un host de dominio que contenga etiquetas numéricas sigue evaluándose como
  dominio si el parser lo identifica como tal.
- El cliente REST puede contactar con el backend local independientemente
  de esta restricción de navegación.

Para la separación entre interpretación y evaluación, se comprobará que:

- El evaluador y sus tests unitarios no dependen de Qt ni de CEF.
- El adaptador utiliza `CefParseURL` de la versión fijada y entrega el resultado
  del análisis y los datos del destino al evaluador.
- Las pruebas del adaptador comprueban la extracción del host y su clasificación
  como dominio, IPv4 o IPv6, así como las URL no evaluables.
- La validación de la barra de direcciones no permite omitir el control en CEF.

Para el alcance del filtrado de navegación, se comprobará que:

- Las reglas se aplican al documento principal y a los subframes, incluidos
  los `iframe` con un destino bloqueado dentro de una página permitida.
- Se cubren la barra, enlaces, formularios, JavaScript, historial y recarga.
- Las redirecciones de las navegaciones cubiertas no permiten visitar un
  destino bloqueado.
- Las navegaciones cubiertas atendidas por service workers mantienen el control.
- Un documento previamente permitido y almacenado en caché no evita el bloqueo
  de una nueva navegación tras aplicar una política que lo prohíba, incluidos
  los recorridos de recarga e historial; se comprobará con CEF.
- Los popups conservan su cancelación conforme a Desktop Foundation.
- La documentación y la evidencia distinguen el filtrado de navegación de
  las solicitudes de recursos y demás tráfico excluido.

Para los esquemas admitidos, se comprobará que:

- Las navegaciones HTTP/HTTPS siguen pasando por la validación y la política.
- `about:blank`, los documentos `about:srcdoc` de subframes y las excepciones
  internas identificadas funcionan sin permitir omitir el control de las
  navegaciones web que originen.
- No se permite cualquier destino por utilizar `chrome:` o `devtools:`;
  cada excepción interna admitida queda identificada y comprobada.
- Las navegaciones a `file:`, `data:`, `blob:` y `ftp:` se bloquean tanto en
  el documento principal como en subframes, también con URL Filtering desactivado.
- Los protocolos externos y los esquemas desconocidos no abren aplicaciones
  del sistema operativo ni se autorizan como destinos de navegación.
- La restricción de navegación no se aplica como un bloqueo general de recursos
  `data:` o `blob:` ni desactiva el JavaScript normal de las páginas.

Para la selección del nombre de descarga, se comprobará que:

- Un nombre sugerido bloqueado cancela la descarga sin abrir «Guardar como».
- Un nombre sugerido permitido permite elegir ubicación y nombre.
- Cambiar el nombre a una extensión bloqueada cancela la descarga y muestra
  el motivo antes de autorizar su continuación.
- La ruta entregada a CEF corresponde al nombre definitivo evaluado y no se
  abre después otro diálogo de CEF que permita cambiarlo.
- Cancelar voluntariamente el diálogo no genera `DOWNLOAD_BLOCKED` ni muestra
  un aviso de bloqueo de política.
- Una descarga permitida con destino confirmado puede completarse; las pruebas
  del evaluador por sí solas no acreditan ese resultado en CEF.

Para los nombres de archivo y la extracción de extensión, se comprobará que:

- La extensión de `informe.PDF` se evalúa como `pdf` y la de `informe.pdf.exe`
  como `exe`, conservando el nombre válido que se vaya a guardar.
- `informe` y `.env` se consideran sin extensión y solo se bloquean por ese
  motivo con Download Control activo.
- Se rechazan nombres vacíos, `.` y `..`, puntos o espacios finales, caracteres
  de control o reservados y nombres reservados de dispositivos, también con extensión.
- `informe.pdf:datos` se rechaza en lugar de interpretar únicamente `pdf` como
  si el resto del nombre no existiera.
- Un nombre inválido se rechaza también con el control desactivado y no se
  transforma automáticamente en otro nombre para permitirlo.
- Las comprobaciones distinguen el nombre de la carpeta seleccionada y
  conservan los nombres Unicode válidos.
- Los mismos criterios se aplican al nombre sugerido y al definitivo antes
  de autorizar la continuación en CEF.

Para la política al confirmar «Guardar como», se comprobará que:

- Un cambio de política durante el diálogo se aplica a la evaluación final,
  aunque el nombre sugerido se hubiera permitido con la política anterior.
- Si la política no es válida o ha caducado al confirmar, se cancela la descarga
  y se muestra el motivo sin llamar a `Continue`.
- El fin de la sesión o un cambio de sesión o instalación impiden autorizar
  la solicitud pendiente con el contexto nuevo.
- La confirmación utiliza la política local vigente sin exigir una consulta
  REST por cada descarga.

Para las descargas autorizadas y en curso, se comprobará que:

- Una transferencia ya autorizada puede terminar durante la misma sesión aunque
  una política posterior bloquee su extensión.
- La caducidad de la política no cancela por sí sola una transferencia ya
  autorizada, pero impide autorizar nuevas descargas o confirmar las pendientes.
- Las solicitudes posteriores a un cambio se evalúan con la política nueva.
- Terminar la sesión o cerrar el navegador cancela las descargas pendientes y
  activas, sin trasladarlas a otra sesión o instalación.
- Una denegación explícita de la instalación también cancela las descargas
  pendientes y activas, aunque se hubieran autorizado con una política vigente.

Para la producción y entrega de eventos, se comprobará que:

- Evaluar una regla no genera eventos como efecto secundario.
- Al aplicar un bloqueo se produce el evento correspondiente y se entrega
  al receptor inyectado.
- El receptor de prueba permite inspeccionar el contenido entregado sin
  dependencias de Qt, CEF, SQLite o red.
- Un fallo de entrega es observable y no cambia la decisión de bloqueo.
- La captura o entrega al receptor no se presenta como prueba de persistencia
  SQLite o sincronización HTTP, que requieren su propia evidencia.
- El motor no asume la gestión de una cola interna de pendientes ni implementa
  almacenamiento o transporte de eventos en sus evaluadores.

Para los datos de `SecurityEvent` y su contexto de origen, se comprobará que:

- El evento contiene UUID, tipo admitido, instante UTC con precisión de
  microsegundos y el detalle estructurado previsto.
- La fecha corresponde al suceso y no se recalcula al entregar o reenviar
  el evento; los reenvíos conservan también UUID, tipo y contenido.
- La hora del sistema utilizada en eventos no sustituye al reloj monotónico
  que determina la vigencia local de la política.
- La entrega al receptor conserva el contexto que produjo el evento, aunque
  haya cambiado la sesión o instalación activa antes de procesarlo.
- El cuerpo REST no añade campos de propietario ni `receivedAt`; se mantiene
  la separación entre el evento y su contexto local de entrega.

Para la producción de `POLICY_UPDATED`, se comprobará que:

- La primera aplicación válida de cada sesión no genera el evento.
- La sustitución por una política diferente genera el evento después de
  aplicarla correctamente, no al recibirla ni antes de aplicarla.
- Renovar la vigencia mediante una consulta de la misma política no genera
  el evento.
- Rechazar una política inválida o fallar al aplicarla no genera el evento.

Para la comparación de políticas, se comprobará que:

- Cambiar el `id` genera el evento tras aplicar la sustitución, aunque la
  configuración coincida con la anterior.
- Cambiar un indicador de activación, modo o contenido de reglas genera el
  evento después de aplicar el cambio, aunque `updatedAt` coincida.
- Reordenar las mismas reglas normalizadas no genera el evento.
- Cambiar únicamente nombre o fechas actualiza los metadatos del snapshot
  sin generar el evento.
- Una consulta y validación correctas renuevan la vigencia aunque `updatedAt`
  no cambie, sin generar el evento si identidad y configuración siguen iguales.
- Una política de otra compañía se rechaza antes de la comparación y no
  genera `POLICY_UPDATED`.

Para la identidad de los bloqueos y sus eventos, se comprobará que:

- Varias notificaciones del bloqueo de la misma operación producen un solo evento.
- Dos intentos distintos de navegación a la misma URL producen eventos con
  UUID diferentes si ambos se bloquean.
- Dos intentos distintos de descarga con el mismo nombre producen eventos con
  UUID diferentes si ambos se bloquean.
- La correlación no confunde intentos distintos por compartir URL, nombre o
  proximidad temporal.
- Las pruebas del bloque Offline comprueban que los reenvíos conservan UUID y
  contenido, sin atribuir al motor la persistencia o gestión de pendientes.

Para el contenido de `details`, se comprobará que:

- Los eventos contienen el motivo controlado y el contexto mínimo previsto
  para su tipo, con referencia de política solo cuando esté disponible.
- Un bloqueo de URL no incorpora credenciales, ruta, parámetros ni la URL
  completa; si no puede interpretarse, tampoco utiliza el texto original.
- Un bloqueo de descarga incluye la extensión evaluada o el motivo de ausencia
  de extensión, sin incorporar el nombre completo ni la ruta del archivo.
- `POLICY_UPDATED` identifica la política aplicada sin copiar sus reglas completas.
- El contenido es compatible con el campo cadena del contrato y no supera
  2.000 puntos de código Unicode.
- El detalle contiene JSON compacto válido con `schemaVersion` igual a `1`
  y un código de motivo estable, sin depender del texto de la interfaz.
- La petición REST representa `details` como una cadena correctamente escapada,
  sin convertirlo en un objeto del cuerpo de la API.
- Los reenvíos conservan el contenido serializado original, sin reconstruirlo
  a partir de la política o del contexto activo posterior.

Estas comprobaciones están pendientes de implementación y ejecución.

## Decisiones arquitectónicas

Las alternativas consideradas, los motivos y las consecuencias de los acuerdos
se recogen en los siguientes ADR:

- [ADR-0012: disponibilidad y vigencia local de la política en Desktop](../adr/0012-desktop-policy-availability.md).
- [ADR-0013: interpretación y alcance de URL Filtering en Desktop](../adr/0013-desktop-url-filtering-boundaries.md).
- [ADR-0014: selección de destino y autorización de descargas en Desktop](../adr/0014-desktop-download-authorization.md).
- [ADR-0015: producción y entrega de eventos del Security Engine](../adr/0015-desktop-security-event-production.md).
