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

## Comprobaciones pendientes

El recorrido funcional de E2 está implementado y el ajuste de demo sin
caducidad ya se ha comprobado. La validación anterior no equivale a completar
la verificación y entrega de E3.

E3 completará la regresión y comprobación manual Debug/Release de la entrega,
las capturas, las pruebas controladas de ausencia de petición HTTP, la distribución
portátil y su guía. La observación de una web pública bloqueada no acredita por
sí sola que no se haya enviado ninguna petición al destino. La vigencia
predeterminada sigue siendo monotónica de 15 minutos y no se renueva por
navegar; la demo utiliza la configuración explícita sin caducidad.

La aceptación de la integración centralizada permanece en #122 y #123; la de
avisos completos y producción de eventos, en #120 y #121. Este registro no cierra
esas issues ni el seguimiento general #127.
