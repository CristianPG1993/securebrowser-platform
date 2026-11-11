# ADR-0013: Interpretación y alcance de URL Filtering en Desktop

- Estado: Aceptada
- Fecha: 2026-11-08

## Contexto

URL Filtering debe evaluar los destinos de navegación sin depender únicamente
de la barra de direcciones: las páginas también pueden iniciar navegaciones
mediante enlaces, formularios, JavaScript y redirecciones.

El motor debe permitir pruebas de sus reglas sin arrancar Qt ni Chromium.
También necesita delimitar el tratamiento de IP y esquemas, preservando los
usos internos necesarios de CEF y el contrato existente del backend.

## Opciones consideradas

| Aspecto | Alternativas y consecuencias |
| --- | --- |
| Direcciones IP | Bloquear literales con el filtrado activo mantiene el MVP centrado en dominios, pero impide navegar a servicios identificados solo por IP. Admitir reglas IP exactas permite esos destinos, pero exige definir normalización y compatibilidad del contrato, especialmente para IPv6. |
| Interpretación de URL | Reutilizar `QUrl` para seguridad aprovecha la barra existente, pero requiere comprobar su compatibilidad con la interpretación de Chromium. Usar `CefParseURL` en un adaptador utiliza el parser de Chromium y exige pruebas específicas de esa frontera. |
| Frames cubiertos | Filtrar solo el documento principal simplifica la integración, pero deja fuera un destino bloqueado cargado en un `iframe`. Cubrir principal y subframes amplía el control y sus pruebas. |
| Esquemas | Limitarse a navegación web y excepciones internas necesarias acota el MVP. Admitir además archivos locales y protocolos externos amplía sus capacidades y requiere controles adicionales. |
| Dominios internacionales | Respetar la regla ASCII recibida conserva el significado publicado por el backend, con posibles diferencias respecto al host de Chromium. Unificar las conversiones de ambas plataformas exigiría ampliar el alcance y modificar también el backend. |

## Decisión

En la tarea #114 se adoptaron las siguientes decisiones.

### Interpretación y evaluación separadas

- El adaptador CEF utilizará `CefParseURL` del SDK fijado para obtener los
  componentes de la URL y clasificará el host como dominio, IPv4 o IPv6.
  `CefParseURL` no entrega directamente esa clasificación.
- `UrlFilter` recibirá datos simples ya interpretados, incluido el resultado
  del análisis, y devolverá una decisión con su motivo. No dependerá de Qt ni
  de CEF ni reinterpretará la URL mediante otro parser.
- `QUrl` conservará su función de ayuda para introducir direcciones en la barra;
  su validación no sustituirá el control de seguridad en CEF.

### Activación y coincidencia de reglas

Las operaciones protegidas requerirán una política válida y vigente para su
contexto aunque `urlFilteringEnabled` sea `false`. Las URL HTTP/HTTPS que no
puedan interpretarse válidamente se bloquearán también con el filtrado desactivado.

Con el filtrado activo se aplicarán la restricción de IP y las reglas de dominio.
Una regla coincidirá con el dominio exacto o con un subdominio respetando el límite
de etiqueta: `example.com` cubrirá `www.example.com`, pero no `myexample.com` ni
`example.com.evil.com`.

Después de las comprobaciones previas, `DENYLIST` bloqueará las coincidencias y
permitirá las restantes; `ALLOWLIST` permitirá las coincidencias y bloqueará las
restantes. Una `DENYLIST` vacía no bloqueará por pertenencia y una `ALLOWLIST`
vacía bloqueará los destinos sometidos al control.

Con el filtrado desactivado se omitirán las reglas de dominio y la restricción
específica de IP, incluso con una `ALLOWLIST` vacía. Se mantendrán la autorización,
la política válida, la interpretación de URL y los límites de esquemas.

### Direcciones IP

- Con `urlFilteringEnabled = true`, se bloquearán las navegaciones HTTP/HTTPS a
  literales IPv4 e IPv6 en ambos modos, aunque la IP aparezca en las reglas.
- Con el filtrado desactivado no se aplicará esta restricción específica; se
  mantiene el requisito de política válida y vigente del ADR-0012.
- Las IP se identificarán sobre el host interpretado y no se compararán por
  subdominios. Los destinos expresados mediante dominio se evaluarán por su nombre.
- La restricción de navegación no impedirá la comunicación del cliente REST
  con el backend local.

### Normalización de dominios

El host obtenido por `CefParseURL` se comparará en ASCII, en minúsculas y sin un
punto final. Por ejemplo, el host de `https://EXAMPLE.COM./` se comparará como
`example.com`, manteniendo la clasificación previa entre dominio e IP.

Los dominios internacionales se compararán mediante su representación ASCII con
la regla normalizada recibida. Por ejemplo, `bücher.de` se comparará mediante
`xn--bcher-kva.de`. No se reconvertirá la regla recibida para introducir alias
o equivalencias adicionales, ni se equipararán caracteres por su parecido visual.

Java y Chromium pueden convertir algunos dominios internacionales de forma
diferente. El MVP respetará el valor que devuelve el backend; si ninguna regla
coincide con el host exacto o un dominio padre, se aplicará el resultado de no
coincidencia del modo configurado. Las diferencias se documentarán y comprobarán
con el CEF fijado, sin modificar la normalización del backend ni afirmar que
ambas conversiones son equivalentes.

### Navegaciones cubiertas

Se cubrirán las navegaciones HTTP/HTTPS del documento principal y los subframes
del único navegador, incluidos los `iframe`, desde la barra, enlaces, formularios,
JavaScript, historial, recarga y redirecciones. Los popups seguirán cancelados
conforme a Desktop Foundation.

Quedarán fuera del filtrado de dominios las solicitudes de imágenes, scripts,
CSS, `fetch`/XHR, WebSocket y las solicitudes autónomas de service workers.
Las navegaciones cubiertas atendidas por un service worker deberán verificarse
igualmente: ese mecanismo no las exime del control.

### Esquemas admitidos

- HTTP/HTTPS se someterán a validación y a la política correspondiente.
- Se preservarán `about:blank` y los documentos `about:srcdoc` de subframes.
  Las navegaciones web que originen seguirán pasando por el control.
- Otros usos internos necesarios de CEF se admitirán únicamente como casos
  identificados y comprobados, sin autorizar globalmente `chrome:` o `devtools:`.
- Se bloquearán `file:`, `data:`, `blob:` y `ftp:` como destinos de navegación,
  tanto en el documento principal como en subframes.
- No se autorizarán navegaciones a protocolos externos como `mailto:` o `tel:`,
  ni a esquemas desconocidos, y no se abrirán aplicaciones externas.
- Este límite de protocolos se mantendrá aunque URL Filtering esté desactivado.

La restricción de destinos no añade un bloqueo general de recursos `data:` o
`blob:` ni desactiva el JavaScript normal. Download Control tratará las descargas
por separado.

## Consecuencias

- La lógica de reglas podrá probarse de forma independiente; el parsing y la
  aplicación real exigirán pruebas adicionales con CEF.
- No se podrá acceder por navegación a servicios identificados únicamente por IP
  con el filtrado activo, ni abrir documentos locales mediante `file:` en el MVP.
- Bloquear un dominio como destino de navegación no garantiza impedir todas las
  solicitudes de red hacia él: las páginas permitidas pueden solicitar recursos
  mediante los mecanismos excluidos.
- Las excepciones internas y la cobertura de redirecciones deberán acreditarse
  durante la integración; su definición documental no demuestra su funcionamiento.
- Desactivar el filtrado omitirá dominios e IP, conservando los requisitos de
  autorización, política vigente, interpretación válida y esquemas admitidos.
- Las pruebas de compatibilidad IDN deberán registrar tanto la regla que devuelve
  el backend como el host obtenido por CEF; una diferencia no generará alias
  silenciosos y podrá cambiar la coincidencia respecto al texto Unicode original.

## Validación

La implementación y las pruebas están pendientes. Se comprobará que:

- GoogleTest prueba el evaluador sin dependencias de Qt o CEF.
- Las pruebas del adaptador verifican componentes, clasificación del host,
  variantes interpretadas como IP y URL no evaluables con el SDK fijado.
- Una política ausente o caducada y una URL HTTP/HTTPS no interpretable impiden
  continuar aunque el filtrado esté desactivado.
- Las coincidencias respetan el dominio exacto o el límite de subdominio y los
  modos y listas vacías producen los resultados acordados.
- Desactivar el filtrado omite dominios e IP, sin omitir los controles generales
  ni ampliar los esquemas admitidos.
- Mayúsculas, un punto final y el ejemplo IDN `bücher.de` se comparan mediante
  los valores ASCII previstos, sin confundir la normalización con la clasificación IP.
- Los casos de integración IDN registran las diferencias entre backend y CEF y
  aplican el modo configurado cuando no coincida ninguna regla, sin añadir alias.
- El filtrado activo bloquea literales IPv4 e IPv6 en ambos modos, sin aplicar
  coincidencia por subdominios a una IP ni confundir dominios con etiquetas numéricas.
- Principal, subframes, orígenes de navegación y redirecciones tienen evidencia
  de aplicación real, incluidas las navegaciones atendidas por service workers.
- Las excepciones internas necesarias funcionan sin abrir una vía para omitir
  el control de las navegaciones web que originen.
- Los destinos y protocolos excluidos se bloquean, no se lanzan aplicaciones
  externas y los popups conservan su cancelación.
- Los recursos excluidos, el JavaScript normal y el cliente REST mantienen el
  funcionamiento acordado; la evidencia distingue el alcance de cada prueba.

## Referencias

- [Tarea #114: contratos, límites y decisiones del MVP](https://github.com/CristianPG1993/securebrowser-platform/issues/114).
- [Diseño del Security Engine](../architecture/security-engine.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [ADR-0011: integración CEF](0011-desktop-cef-integration.md).
- [ADR-0012: disponibilidad y vigencia local de la política](0012-desktop-policy-availability.md).
- [Parser de CEF fijado](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/libcef/common/parser_impl.cc).
- [Conversión IDN de Java](https://docs.oracle.com/en/java/javase/25/docs/api/java.base/java/net/IDN.html).
- [Conversión IDN de Chromium](https://raw.githubusercontent.com/chromium/chromium/main/url/url_idna_icu.cc).
