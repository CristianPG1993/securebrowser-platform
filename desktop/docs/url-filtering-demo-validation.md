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

## Comprobaciones pendientes

E2 añadirá los dominios definitivos, el selector dentro de la ventana, el
indicador permanente, los avisos Qt y la acción de recarga explícita.

E3 completará la regresión y comprobación manual Debug/Release de la entrega,
las capturas, las pruebas controladas de ausencia de petición HTTP, la distribución
portátil y su guía. La observación de una web pública bloqueada no acredita por
sí sola que no se haya enviado ninguna petición al destino. La vigencia sigue
siendo monotónica de 15 minutos y no se renueva por navegar.

La aceptación de la integración centralizada permanece en #122 y #123; la de
avisos completos y producción de eventos, en #120 y #121. Este registro no cierra
esas issues ni el seguimiento general #127.
