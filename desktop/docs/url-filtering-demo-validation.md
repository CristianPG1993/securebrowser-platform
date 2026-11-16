# Validación de URL Filtering con políticas demo

Registro de la entrega autónoma [#131](https://github.com/CristianPG1993/securebrowser-platform/issues/131).
Las políticas son locales y utilizan el motor existente. Este recorrido no
acredita autenticación, adquisición de Policy desde el backend ni sincronización.

## E1 — Primera navegación real

Fecha de comprobación: 14 de noviembre de 2026.

- Base: `5222bee4b6b19c53299e19894c704aa760f6a2a3`, PR #130 integrada.
- Rama: `feature/url-filtering-demo`.
- Fuente y pruebas: `62a9f8b31cead5b644e019dc3b47267563128ddd`.
- Conexión a la sesión: `6063cba401dd3b43e1114c5c3b58a71051be674b`.
- La comprobación se realizó sobre el árbol de trabajo conservado después en
  esos commits; no se atribuye al binario una revisión estampada que no contiene.

El arranque interpreta opciones explícitas y la demo utiliza un perfil CEF
separado. `BrowserSession` crea el `PolicyManager`, prepara el contexto demo,
publica el snapshot y entrega ese mismo gestor a `SecurityRequestHandler`.
La fuente no evalúa dominios y el modo normal no carga políticas demo.

En E1, la política libre desactiva el filtrado de dominios. La restrictiva activa
DENYLIST con la regla mínima `youtube.com`, incluidos sus subdominios. Ambas
mantienen la misma configuración de descargas desactivada. Los demás dominios
definitivos de la restrictiva se incorporarán en E2.

### Resultado manual en Debug

| Política | Destino | Resultado confirmado |
| --- | --- | --- |
| Libre | `https://www.wikipedia.org` | Carga permitida. |
| Libre | `https://www.youtube.com` | Carga permitida. |
| Restrictiva | `https://www.wikipedia.org` | Carga permitida. |
| Restrictiva | `https://www.youtube.com` | Navegación bloqueada. |

Cada cambio se comprobó cerrando la instancia anterior y arrancando con la
otra política. La comprobación utiliza nuevas navegaciones; no exige retirar
una página que ya estuviera visible. El selector y el aviso Qt no forman parte
de esta primera comprobación.

### Evidencia automatizada y de diagnóstico

- Los informes CEF de Debug y Release registran 128 casos, cero fallos y cero
  errores, con fecha 14 de noviembre de 2026. Incluyen las doce pruebas de las
  opciones de arranque. Se conservan en
  `desktop/build/cef-tests/cef-adapter/<configuración>/cef-adapter-results.xml`.
- El último `desktop/build/tests/Testing/Temporary/LastTest.log` registra las
  seis pruebas `DemoPolicySourceTest` superadas en Debug. La selección ejecutó
  seis casos; los 281 descubiertos no equivalen a una regresión completa ejecutada
  en esa pasada.
- Las pruebas de las políticas utilizan `PolicyManager` y `UrlFilter` reales,
  con reloj controlado para el límite de 15 minutos y la republicación explícita.
  No realizan peticiones HTTP ni inicializan CEF.
- El log de aplicación `desktop-1794676306235-30548.log`, bajo
  `%LOCALAPPDATA%\SecureBrowser\Logs`, confirma la ejecución restrictiva Debug,
  publicación local, bloqueos aplicados y cierre normal. El permiso de las páginas
  se acredita mediante la comprobación manual; ese log no registra sus URL.

Extracto controlado del diagnóstico restrictivo:

```text
Application startup mode: demo; requested policy: restricted.
Demo policy published; source: local-demo; preset: restricted; policy_id: 2.
CEF navigation blocked; operation_id: 1; reason: 8; main_frame: 1.
Qt window destroyed; event loop exit code 0.
CEF shutdown completed.
```

En esta revisión, el motivo `8` corresponde a `SecurityReason::DomainDenied`.
Los identificadores demo no acreditan un Device autenticado.

## E2 — Cambio de política desde la ventana

Fecha de comprobación: 15 de noviembre de 2026.

La restrictiva incorpora los siete dominios acordados: `facebook.com`,
`instagram.com`, `tiktok.com`, `x.com`, `youtube.com`, `twitch.tv` y
`netflix.com`, incluidos sus subdominios mediante el evaluador existente.
Las reglas y sus pruebas ampliadas se conservan en `c2087c3`.

El panel inferior muestra el modo demostración, el nombre publicado y un
selector de las dos políticas. El selector solicita la publicación al mismo
`PolicyManager` que consulta CEF, conservando el contexto original. El nombre
y la selección solo se confirman después de una publicación correcta. Un rechazo
conserva la confirmación anterior; durante el cierre se retira el receptor.
El modo normal no ofrece este selector ni carga políticas locales.

### Resultado manual en Debug

Se confirmó el siguiente recorrido sin cerrar el ejecutable, tras compilar y
arrancar con `--demo-url-filtering --demo-policy=free`:

| Paso | Comprobación | Resultado confirmado |
| --- | --- | --- |
| Libre | Solicitar Wikipedia y YouTube. | Ambas páginas cargan. |
| Cambiar a Restrictiva | Comprobar el nombre y solicitar de nuevo ambas páginas. | El panel cambia; Wikipedia carga y YouTube se bloquea. |
| Volver a Libre | Solicitar de nuevo YouTube. | La página vuelve a cargar. |

El resultado procede de la comprobación manual comunicada sobre el árbol de
trabajo de esta entrega. No se atribuyen a esta revisión capturas, nuevos
informes automatizados ni una regresión completa no aportados. La comprobación
del selector en Release continúa pendiente.

En la revisión de esta primera comprobación, cambiar de política constituía
una publicación explícita con otro instante monotónico de validación y un
plazo de 15 minutos. Leer el estado o navegar no lo renovaba. No se comprobó
esa caducidad esperando tiempo real. El ajuste posterior de alcance se recoge
más abajo. El cambio de política afecta a nuevas navegaciones y no retira
una página que ya estuviera visible; elegir otra vez la opción confirmada
tampoco la republica.

### Botón Inicio

Se añadió Inicio a la barra de navegación y se confirmó manualmente su
funcionamiento en Debug. La acción recorre `RequestHome`, el receptor de
`BrowserNavigation`, la tarea CEF `GoHome` y `Navigate(HomeUrl())`.
`HomeUrl()` conserva `https://example.com/` como destino compartido con el
arranque; la solicitud utiliza el mismo handler y la política vigente, sin
excepciones de seguridad ni renovación del plazo de validación.

La comprobación en Release queda pendiente. El icono estándar se representa
como carpeta en el tema de Windows utilizado; su sustitución por una casa se
aplazó. La acción «Ir a inicio» del aviso de bloqueo se añadió en el bloque
posterior.

### Aviso Qt de bloqueo

Se confirmó en Debug el aviso con el motivo, el dominio y el nombre de la
política. «Ir a inicio» reutiliza la acción de la barra; el mensaje se retira
cuando CEF confirma otro documento principal o se publica otra política.
Los avisos se copian a la cola Qt y se comprueba su correspondencia con el
estado antes de mostrarlos. Esta comprobación precede al ajuste de vigencia
descrito a continuación.

### Ajuste de alcance: demo sin caducidad

El 15 de noviembre de 2026 se aprobó que las políticas de la demostración
autónoma no caduquen por tiempo. La composición construye explícitamente ese
gestor con `PolicyManager::Validity::Unlimited`; el modo normal y los
constructores sin opción conservan la vigencia monotónica de 15 minutos.
La configuración pertenece a cada gestor y no puede cambiarse desde el
selector ni a través de un snapshot.

La interfaz identifica la política demo como «sin caducidad». Se descarta el
botón de recarga por vencimiento y su indicador periódico, que ya no son
necesarios para este recorrido. La validación de políticas, las guardas de
contexto y las decisiones del motor siguen siendo obligatorias. No se añade
una política local como sustitución del modo conectado.

Se prepararon pruebas con reloj controlado para la disponibilidad de ambas
políticas demo tras 15 minutos y un día adicional, la conservación de sus
decisiones de dominio y el bloqueo sin política o tras cerrar el contexto.
Otra prueba compara un gestor ilimitado con uno predeterminado en el límite
exacto de 15 minutos; este último debe caducar.

### Validación del ajuste sin caducidad

Se confirmó el funcionamiento tras compilar las pruebas de seguridad y el
ejecutable en Debug y Release. La comprobación manual incluyó el indicador
«sin caducidad», Wikipedia permitida, YouTube bloqueado con la restrictiva
y el cambio a Libre dentro de la misma aplicación.

El último `desktop/build/tests/Testing/Temporary/LastTest.log` disponible
corresponde a Release y registra 283 pruebas superadas y cero fallos. Incluye
los casos nuevos de vigencia ilimitada y aislamiento del límite predeterminado.
Ese archivo conserva la última pasada; no se presenta como informe separado
de Debug. La comprobación del tiempo transcurrido se realiza con reloj
controlado, sin esperar 15 minutos durante la prueba manual.

## E3 — Regresión y evidencia HTTP controlada

Tras el ajuste sin caducidad, se confirmaron las pruebas CEF en Debug y Release.
Los informes `cef-adapter-results.xml` de ambas configuraciones, con fecha
15 de noviembre de 2026, registran 128 casos, cero fallos y cero errores.

Se preparó después
`DemoPolicyNavigationTest.SwitchingDemoPoliciesControlsActualHttpRequests`,
que utiliza los snapshots originales, el mismo gestor/contexto demo y el
servidor CEF existente. La resolución exclusiva del ejecutable de pruebas
dirige `twitch.tv` a loopback. Libre debe producir HTTP 200 y una petición;
Restrictiva debe cancelar con `DomainDenied` y cero peticiones nuevas; al
volver a Libre debe recibirse la segunda petición. El XML registra tanto
los recuentos acumulados como el incremento de cada etapa.

El [procedimiento manual con el EXE](../tests/manual/url-filtering-demo/README.md)
usa una página estática y la herramienta HTTP estándar de Python del equipo
de desarrollo. No incorpora servidor, resolución de pruebas ni Python al
paquete normal de la tutora. Los dos controles positivos y el aviso Qt son
obligatorios: una página inaccesible por DNS o conexión no acredita bloqueo.

El 16 de noviembre de 2026, los informes de Debug y Release registran 129
casos, cero fallos y cero errores. Incluyen la prueba nueva, con propiedades
`FreeBeforeNewRequests=1`, `RestrictedNewRequests=0` y `FreeAfterNewRequests=1`,
y un recuento final de dos peticiones. Se confirma así el recorrido automático
con los snapshots reales y sin solicitudes HTTP del paso restringido.

### Comprobación HTTP manual en Debug

El 16 de noviembre de 2026 se completó la pasada `debug-final-02` con el EXE
Debug, en una misma ventana y cambiando la política desde el selector:

| Paso | Peticiones HTTP recibidas | Resultado |
| --- | --- | --- |
| Libre inicial | 1 GET de `free-before`, HTTP 200 | Página local cargada. |
| Restrictiva | 0 GET de `restricted` | `DomainDenied` y aviso Qt. |
| Libre final | 1 GET de `free-after`, HTTP 200 | Página local cargada de nuevo. |

El log `desktop-1794809261400-19304.log` confirma la publicación de las dos
políticas, el bloqueo del documento principal con `operation_id=1`, la
presentación del aviso y el cierre con código cero seguido de `CefShutdown`.
Las peticiones del registro HTTP corresponden exclusivamente al identificador
de esta pasada, separado de los intentos iniciales de preparación.

Los artefactos locales se conservan en
`desktop/build/url-filtering-demo-evidence/Debug/debug-final-02/`: registro
HTTP original, extracto de peticiones, log de aplicación, informe CEF y
`summary.json` con los recuentos y SHA-256 de los registros, el EXE y su DLL.
Se incorpora una [captura del aviso Qt](evidence/url-filtering-demo/qt-block-notice-debug-final-01.png)
como evidencia visual de apoyo. La página visible pertenece a la pasada anterior
`debug-final-01`; no se atribuye la captura al recuento HTTP de `debug-final-02`.
El archivo original se conserva sin editar y su SHA-256 se registra en el resumen.
La comprobación de Release se recoge a continuación, con sus registros propios.

### Comprobación HTTP manual en Release

El 16 de noviembre de 2026 se completó la pasada `release-final-01` con el EXE
Release y el selector de la misma ventana. El registro HTTP confirma un GET
`free-before` con estado 200, cero GET `restricted` y un GET `free-after` con
estado 200.

El log `desktop-1794809809521-3336.log` registra la configuración Release,
las publicaciones Libre → Restrictiva → Libre, dos intentos cancelados con
`DomainDenied` y sus avisos Qt (`operation_id=1` y `operation_id=2`). Ninguno
de esos intentos llegó al servidor HTTP. El proceso terminó con código cero
y completó `CefShutdown`.

Los registros originales, su extracto HTTP, el informe CEF y el resumen con
SHA-256 se conservan en
`desktop/build/url-filtering-demo-evidence/Release/release-final-01/`.
La [captura de Release](evidence/url-filtering-demo/qt-block-notice-release-final-01.png)
muestra la dirección restringida y el aviso de bloqueo. El contenido que
permanece debajo de la barra corresponde a una página anterior, no a una
carga del destino restringido.

## E3 — Historial y entrega portátil

### Corrección del historial dentro del mismo documento

El 16 de noviembre de 2026 se detectó manualmente un recorrido no cubierto
por la regresión anterior: abrir un vídeo de YouTube con la política Libre,
cambiar a la restrictiva y utilizar Atrás y Adelante entre el vídeo y la
portada permitía continuar utilizando ese contenido. La aceptación del
paquete quedó detenida hasta incorporar y verificar la corrección.

Las pruebas anteriores del historial utilizan documentos completos en hosts
distintos. No acreditan el historial creado dentro del mismo documento.
En esa versión la aplicación tampoco reevaluaba el documento abierto al
publicar otra política desde el selector. Se incorporó una comprobación
compartida en el handler y su conexión desde el selector y los avisos de
dirección de CEF. Si el documento está bloqueado, se solicita sustituirlo por
`about:blank`, conservando el aviso Qt y sin permitir que una confirmación de
abandono mantenga abierta la página revocada. La aceptación exige completar
la regresión en ambas configuraciones y repetir la comprobación portátil.
El paquete inicial no incorpora la corrección; se sustituyó por la entrega
verificada que se identifica a continuación.

Tras compilar la conexión, se confirmó manualmente en Debug que el cambio
de política retira la página abierta y que Atrás y Adelante no permiten
seguir utilizando YouTube. Tras compilar la aplicación Release actualizada
se confirmó también el mismo recorrido manual en esa configuración.

Se han preparado once casos automatizados con `BrowserClient` de producción:
reevaluación explícita, Atrás y Adelante desde CEF y JavaScript, controles
permitidos y retorno al historial después de retirar el documento. La página
local crea una entrada con `history.pushState`; las aserciones comprueban el
reemplazo del contexto JavaScript, los avisos y la ausencia de peticiones a
la ruta añadida al historial. Los once casos se han validado en Debug y Release.

La preparación utiliza `Runtime.evaluate` con `userGesture` explícito y una
comprobación de `navigator.userActivation.hasBeenActive`. Solo prepara el
documento local de pruebas; los permisos siguen evaluándose en el cliente y
el motor de producción.

La ejecución posterior de Debug del 16 de noviembre de 2026 registra **140
pruebas, cero fallos y cero errores**, incluidos los once casos de
`SameDocument/DocumentPolicyChangeTest`. El XML
`desktop/build/cef-tests/cef-adapter/Debug/cef-adapter-results.xml`, con
timestamp `2026-11-16T09:32:25.332`, confirma el resultado. Las propiedades
registran la entrada de historial y la activación de usuario antes de
comprobar la conservación o sustitución del documento.

El informe Release del mismo día, con timestamp `2026-11-16T09:34:47.442`,
registra también **140 pruebas, cero fallos y cero errores**, incluidos los
once casos nuevos. Se conserva en
`desktop/build/cef-tests/cef-adapter/Release/cef-adapter-results.xml`.
### Paquete portátil comprobado

El 16 de noviembre de 2026 se generó y comprobó
`SecureBrowser-demo-Release-20261116-094631-65a1330f.zip`, conservado en
`desktop/build/packages/`. Su SHA-256 es:

```text
1FEF5FB96AE07C12F64EE4E67A6B26F11F5C85B9AB2387D5275FCEB8BA66AD13
```

Se verificaron tamaño y SHA-256 de los 310 archivos registrados en su
manifiesto, tanto dentro del ZIP como en la copia extraída en el Escritorio,
fuera del repositorio. La DLL coincide con el Release corregido. El paquete
incluye las dependencias de ejecución, los avisos de terceros, el lanzador
explícito y la [guía breve](../distribution/LEEME-demo.txt).

La comprobación manual se realizó abriendo `Iniciar-demo.cmd` en esa copia:

| Paso | Resultado confirmado |
| --- | --- |
| Libre, reproducción de YouTube | Carga y reproducción permitidas. |
| Cambio a Restrictiva con el vídeo abierto | Documento retirado y aviso Qt. |
| Atrás y Adelante bajo Restrictiva | El vídeo no vuelve a estar disponible. |
| Wikipedia bajo Restrictiva | Carga permitida. |
| Vuelta a Libre y retorno por el historial | YouTube vuelve a estar permitido. |
| Cierre mediante la X | Bucle Qt con código cero y `CefShutdown` completado. |

El log `desktop-1794819087353-26688.log` confirma Release, el modo demo,
las publicaciones Libre → Restrictiva → Libre, la retirada del documento,
tres bloqueos `DomainDenied` con sus avisos y el cierre coordinado. No
registra las URL; la reproducción y la carga de Wikipedia se acreditan por
la comprobación visual, no se deducen del log.

Los artefactos se conservan en
`desktop/build/url-filtering-demo-evidence/Release/portable-final-01/`:
log de aplicación, manifiesto, verificación del paquete, hash del ZIP,
informes CEF Debug/Release y `summary.json`. Las capturas anteriores
documentan el aviso Qt; no se presentan como capturas de esta última pasada.

La copia portátil se comprobó en el equipo Windows de desarrollo fuera del
proyecto. No se atribuye a esta pasada una comprobación en una máquina limpia
sin SDK instalados. La preparación reproducible se recoge en el
[README de distribución](../distribution/README.md).

### Estado de la entrega

La validación funcional de E1–E3 queda completada en el entorno registrado:
HTTP controlado, cambio de políticas, avisos Qt, regresión y copia portátil.
Quedan la revisión de los commits, la PR y sus comprobaciones de CI antes
de integrar la entrega. No se ha realizado un merge automático.

La vigencia predeterminada sigue siendo monotónica de 15 minutos y no se
renueva por navegar; la demo utiliza la configuración explícita sin caducidad.

La aceptación de la integración centralizada permanece en #122 y #123; la de
avisos completos y producción de eventos, en #120 y #121. Este registro no cierra
esas issues ni el seguimiento general #127.
