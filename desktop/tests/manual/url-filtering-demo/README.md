# Comprobación HTTP local de la demo

Este procedimiento comprueba el EXE real con sus dos políticas originales.
Solo la resolución de esta ejecución dirige `twitch.tv` a loopback; no se
modifican las reglas, el archivo hosts de Windows ni el motor de seguridad.
La herramienta HTTP de Python se utiliza en el equipo de desarrollo para
recoger evidencias. No es una dependencia del paquete para la tutora.

## Preparación

Cerrar las instancias anteriores de SecureBrowser. Desde la raíz del repositorio,
en una primera terminal, iniciar el servidor limitado a este directorio y a IPv4
loopback. El puerto 8765 debe estar libre:

```powershell
python -u -m http.server 8765 --bind 127.0.0.1 --directory desktop/tests/manual/url-filtering-demo 2> desktop/build/url-filtering-demo-http-Debug.log
```

El registro se sobrescribe al iniciar esta pasada. La terminal queda ocupada
hasta detener el servidor con Ctrl+C.

En otra terminal, abrir el EXE con Libre:

```powershell
.\desktop\build\Debug\SecureBrowser.exe --demo-url-filtering --demo-policy=free '--host-resolver-rules=MAP twitch.tv 127.0.0.1' --no-proxy-server
```

Para Release, sustituir `Debug` por `Release` en el EXE y el nombre del registro,
y usar `run=release-final-01` en las direcciones siguientes. Los argumentos de
resolución y proxy corresponden a esta comprobación explícita, no al arranque
portátil normal.

## Recorrido y aceptación

**La política se cambia en el selector de Qt.** `case` y `run` solo identifican
las peticiones del registro; no seleccionan una política. Antes de navegar,
comprobar que el panel inferior muestra el nombre de la política elegida.

Copiar únicamente la URL completa de cada bloque a la barra del navegador:

1. Seleccionar Libre, pegar la dirección y pulsar Enter. Debe aparecer
   «Destino local recibido», «Paso cargado: free-before» y registrarse un GET con estado 200:

   ```text
   http://twitch.tv:8765/index.html?case=free-before&run=debug-final-01
   ```

2. Seleccionar «Restricción de redes sociales y entretenimiento». El documento
   local abierto se retira y aparece el aviso Qt de bloqueo de `twitch.tv`.
   Pegar después la dirección y pulsar Enter: sigue bloqueada, sin un GET
   de esta dirección:

   ```text
   http://twitch.tv:8765/index.html?case=restricted&run=debug-final-01
   ```

3. Volver a seleccionar Libre, pegar la dirección y pulsar Enter. Esperar hasta
   ver «Paso cargado: free-after» antes de cerrar. Debe registrarse otro GET con estado 200:

   ```text
   http://twitch.tv:8765/index.html?case=free-after&run=debug-final-01
   ```

El HTML muestra el paso y el identificador que se han cargado realmente. Si la
política nueva bloquea el documento abierto, se sustituye por `about:blank` y
se conserva el aviso Qt. Se exige además la ausencia de la petición restringida.
Cambiar el selector no solicita la URL etiquetada como `restricted`; esa
dirección debe introducirse y enviarse expresamente.
Al repetir una pasada, cambiar `run` por un identificador no utilizado, por
ejemplo `debug-final-02`, para que la caché de una pasada anterior no elimine los
controles positivos. No copiar variables de PowerShell como parte de una URL.

4. Cerrar el EXE normalmente, detener el servidor con Ctrl+C y consultar el registro:

```powershell
Select-String -Path desktop/build/url-filtering-demo-http-Debug.log -Pattern 'GET /index.html'
```

La evidencia exige los dos GET permitidos y la ausencia del GET restringido,
junto con el aviso Qt y el bloqueo aplicado registrado en el log de la aplicación
bajo `%LOCALAPPDATA%\SecureBrowser\Logs`. Un error de DNS, TLS o conexión, o la
ausencia del control positivo con HTTP 200, no acredita el resultado.

Conservar el registro HTTP, el log de aplicación y las capturas de las tres
etapas. Repetir el procedimiento en cada configuración, archivando el registro
antes de iniciar la siguiente pasada.

## Prueba automática equivalente

`DemoPolicyNavigationTest.SwitchingDemoPoliciesControlsActualHttpRequests`
reutiliza el servidor CEF existente, la ruta sin caché `/action-target` y los
snapshots reales de `DemoPolicySource`. Exige HTTP 200 en Libre, `DomainDenied`
en Restrictiva y recuentos acumulados 1 → 1 → 2. El XML registra esos recuentos
y las peticiones nuevas de cada etapa: 1 → 0 → 1.
La prueba utiliza navegadores independientes con un mismo gestor y contexto;
no sustituye la comprobación de la ventana Qt y su selector en el EXE.
