# Comprobación manual de Download Control y Qt

Procedimiento para el EXE real y las políticas locales de la tarea #119.
Complementa las pruebas automatizadas con la interacción del diálogo y los
avisos de Qt. Las comprobaciones realizadas en Debug y Release se recogen al final.

La herramienta utiliza Python solo en el equipo de desarrollo. No forma parte
del runtime ni es una dependencia del ZIP de demostración. Todos los archivos
servidos contienen texto inerte; sus nombres permiten comprobar las reglas.

## Preparación

Desde la raíz del repositorio, iniciar el servidor en una terminal:

```powershell
python -u desktop/tests/manual/download-control-demo/serve-downloads.py
```

Mantener esa terminal abierta. El servidor escucha únicamente en
`127.0.0.1:8767`; Ctrl+C lo detiene. Si el puerto está ocupado, detener la
instancia anterior antes de repetir.

En otra terminal, abrir la aplicación Debug con la política restrictiva y
dirigir el dominio de prueba `downloads.test` al servidor local:

```powershell
.\desktop\build\Debug\SecureBrowser.exe --demo-url-filtering --demo-policy=restricted '--host-resolver-rules=MAP downloads.test 127.0.0.1' --no-proxy-server
```

Introducir esta dirección en la barra y comprobar que aparece la página local:

```text
http://downloads.test:8767/
```

Preparar una carpeta vacía del usuario para los destinos de esta pasada y
utilizar un nombre distinto en cada descarga. Repetir después con el EXE de
`Release` y otra carpeta. El mapeo solo afecta a esta ejecución; no modifica
el archivo hosts ni las reglas de la política. Navegar mediante el dominio
permite mantener el bloqueo de direcciones IP literales de la restrictiva.

## Diálogo, nombres y avisos

Antes de cada paso, comprobar el nombre de la política en el panel Qt.
El selector cambia la política; los enlaces de la página solo solicitan archivos.

| Política | Acción | Resultado esperado |
| --- | --- | --- |
| Restrictiva | Abrir PDF permitido y guardar como `informe.pdf`. | Un único diálogo Qt, nombre sugerido correcto, archivo con el texto `SBP manual download fixture.` y mensaje «Descarga completada». |
| Restrictiva | Abrir EXE bloqueado y después MSI bloqueado. | Sin diálogo de destino ni archivo final; aviso Qt de extensión bloqueada. |
| Restrictiva | Abrir PDF permitido y cambiar el nombre a `programa.exe`. | El diálogo permite editar el nombre; al confirmar, se bloquea y no aparece el archivo final. |
| Restrictiva | Abrir PDF permitido y pulsar Cancelar. | Se cierra el diálogo y no aparece un archivo final ni un aviso nuevo de bloqueo o error. |
| Restrictiva | Abrir nombre Unicode y guardar conservando `informe-niño.PDF`. | Nombre y mayúsculas conservados, con el mismo contenido de texto. |
| Libre | Abrir PDF permitido y guardar con el nombre `libre.exe`. | Se permite ese destino válido al estar desactivado el control de extensiones. |
| Restrictiva | Abrir Fallo provocado. Si aparece Guardar como, confirmar un destino nuevo. | Aviso de error operativo, sin presentarlo como un bloqueo de política ni dejar un archivo final completo. |

Un aviso anterior puede seguir visible tras Cancelar. La comprobación exige que
esa cancelación no genere otro bloqueo. Una respuesta HTTP 200 en el servidor
solo acredita entrega de datos al navegador, no la autorización ni la creación
del archivo final. Comprobar ambos en la aplicación y en la carpeta de destino.

El resultado operativo se muestra en un mensaje no modal separado del panel de
bloqueos. Se retira al abrir otra petición de guardado o comenzar una nueva
carga de página, incluida una recarga. La transferencia incompleta se provoca
anunciando más bytes de los
que se envían, junto con un ETag fuerte y una respuesta HTTP/1.1. Con HTTP/1.0,
Chromium no considera fuerte ese validador y puede aceptar el contenido corto.
El error puede detectarse mientras el diálogo todavía está pendiente; en ese
caso debe cerrarse y mostrarse el resultado.

## Descargas blob: y data:

La página genera el mismo texto inerte en memoria y lo ofrece mediante cuatro
enlaces con atributo `download`. El nombre sugerido procede de ese atributo;
estos archivos no tienen una respuesta HTTP ni un `Content-Disposition`.
Mantener abierta la página mientras se resuelve el diálogo y usar destinos nuevos.

Con la política Restrictiva, comprobar en Debug y Release:

| Enlace | Nombre sugerido | Resultado esperado |
| --- | --- | --- |
| Blob — nombre PDF | `blob-permitido.pdf` | Guardado con el texto fijo y aviso de descarga completada. |
| Blob — nombre EXE | `blob-bloqueado.exe` | Aviso de extensión bloqueada, sin diálogo ni archivo final. |
| Data — nombre PDF | `data-permitido.pdf` | Guardado con el texto fijo y aviso de descarga completada. |
| Data — nombre EXE | `data-bloqueado.exe` | Aviso de extensión bloqueada, sin diálogo ni archivo final. |

Esta comprobación cubre las descargas mediante esos enlaces desde la página
HTTP local. No acredita navegación a documentos `blob:` o `data:`, que se
evalúa por separado mediante URL Filtering, ni descargas desde otros orígenes.

La vía `CefBrowserHost::StartDownload` se comprueba en la batería CEF de Debug y
Release mediante `DownloadTestTrigger::StartDownload`. Omite `CanDownload`, pero
mantiene las comprobaciones de nombre y destino en `OnBeforeDownload` y al
resolver el diálogo. La página manual no invoca ese método nativo.

## Transferencia en curso y cierre

La descarga lenta envía el mismo texto repetido en bloques durante unos
30 segundos. Confirmar el destino con rapidez y observar que el archivo empieza
a recibir datos antes de cambiar la política o cerrar la ventana.

1. En Libre, iniciar el PDF lento y elegir `en-curso.exe`. Cuando empiece la
   transferencia, seleccionar Restrictiva. Debe poder terminar en el mismo
   contexto aunque la regla nueva bloquee las solicitudes nuevas de `exe`.
   Al terminar debe mostrarse también «Descarga completada», aunque haya cambiado la política.
2. Iniciar otra descarga lenta, confirmar un destino nuevo y cerrar la ventana
   mientras recibe datos. La aplicación debe cerrar normalmente y no debe
   quedar un archivo final completo de esa transferencia cancelada.

El servidor puede recibir una desconexión o terminar de enviar datos antes de
detectar el cierre. Eso no sustituye la comprobación del destino ni del cierre
de CEF. No se exige ausencia absoluta de bytes temporales de Chromium.

## Copia extraída del ZIP

Con el servidor manual iniciado desde el repositorio, abrir `Iniciar-demo.cmd`
en la copia extraída fuera de `build`. Comprobar el nombre de la carpeta para
utilizar el paquete recién generado. El lanzador selecciona Libre; cambiar a
Restrictiva y abrir `http://localhost:8767/` para repetir las comprobaciones HTTP.
Esta dirección permite usar el lanzador sin añadir el mapeo de `downloads.test`.
Python y la página de pruebas permanecen en el equipo de desarrollo; el ZIP no
los necesita para ejecutar la aplicación.

## Evidencia

Anotar configuración, política, acción, nombre elegido y resultado observado.
Conservar capturas del diálogo y de los avisos, comprobar los archivos finales
y revisar el log de aplicación bajo `%LOCALAPPDATA%\SecureBrowser\Logs`.
No atribuir a una ejecución resultados que todavía no se hayan comprobado.

### Comprobaciones registradas en Debug

El 19 de noviembre de 2026, el usuario confirmó durante este recorrido:

- Descarga permitida y conservación del nombre Unicode.
- Bloqueo de EXE y MSI sugeridos, y del renombrado final a EXE.
- Cancelación voluntaria sin archivo nuevo ni aviso de bloqueo.
- Guardado con extensión EXE en Libre.
- Finalización de una transferencia autorizada tras cambiar a Restrictiva.
- Cierre durante una transferencia: cierre normal de Qt y CEF y ausencia del
  archivo de destino comprobados también en el registro y la carpeta local.
- Después de incorporar los avisos de resultado: mensaje de descarga completada,
  mensaje de error con la respuesta incompleta y retirada del aviso al recargar.

La corrección de separadores nativos está en `8717f4c`. Un archivo permitido
de prueba se comprobó con 29 bytes y el texto esperado.

### Avisos comprobados en Release

El 19 de noviembre de 2026, sobre el código de `017e086`, el usuario confirmó:

- PDF permitido: archivo guardado y mensaje «Descarga completada».
- Respuesta incompleta: mensaje de error de descarga.
- Recarga de página: retirada del mensaje anterior.
- Cancelación voluntaria: sin archivo nuevo ni aviso de error.

Esta pasada de Release acredita esos cuatro casos; el recorrido ampliado de
Unicode, reglas y cierre durante transferencia está registrado arriba en Debug.

### Regresión automática después de los avisos

El usuario compiló y ejecutó la batería existente sobre `017e086` en ambos modos.
Los XML locales registran el 19 de noviembre de 2026:

| Configuración | Casos | Fallos | Errores |
| --- | ---: | ---: | ---: |
| Debug | 196 | 0 | 0 |
| Release | 196 | 0 | 0 |

Informes: `desktop/build/cef-adapter/Debug/cef-adapter-results.xml` y
`desktop/build/cef-adapter/Release/cef-adapter-results.xml`. Los binarios y los
XML permanecen en `build/`; no se incorporan al repositorio.

### Regresión tras conectar la retirada del contexto

El 19 de noviembre de 2026 se compiló la aplicación, el núcleo de seguridad y
la batería CEF con la suscripción a la retirada del contexto. El usuario confirmó
los 448 registros de CTest superados en Debug y Release: 447 casos del núcleo y
una entrada que ejecuta los 196 casos de CEF. Los XML de CEF de ambas
configuraciones registran cero fallos y cero errores.

Se adaptaron los seis casos existentes de `PendingContext`, sin añadir casos.
Registran el aviso de retirada del contexto y retienen la respuesta del diálogo
hasta que el handler solicita cerrarlo. Después entregan una ruta válida como
respuesta tardía y comprueban cancelación, un único aviso de `InvalidContext`
y ausencia del archivo final. Esta comprobación cubre las solicitudes con
diálogo pendiente; no acredita por sí sola la retirada durante una transferencia
ya autorizada.

### Descargas blob: y data: comprobadas en Debug y Release

El 19 de noviembre de 2026, con la página manual ampliada y la política
Restrictiva, el usuario confirmó los cuatro casos en ambas configuraciones:

- PDF mediante `blob:`: guardado, contenido fijo correcto y aviso de finalización.
- EXE mediante `blob:`: aviso de extensión bloqueada, sin diálogo ni archivo final.
- PDF mediante `data:`: guardado, contenido fijo correcto y aviso de finalización.
- EXE mediante `data:`: aviso de extensión bloqueada, sin diálogo ni archivo final.

### Paquete portátil comprobado

El 19 de noviembre de 2026 se generó el paquete sobre la revisión `b9d5b47`:

`SecureBrowser-demo-Release-20261119-125031-3c3e9f46.zip`

Se conserva localmente en `desktop/build/packages/`. Su SHA-256 es:

```text
C4A6E0862F8F21AD02B32C85B8EF157EDA1462F1C03E62CD9F27B8BCC24F5707
```

Se verificaron tamaño y SHA-256 de los 310 archivos del manifiesto dentro del
ZIP y en la copia extraída del Escritorio, fuera del repositorio. El ZIP contiene
además el propio manifiesto, y conserva los 242 archivos requeridos por el
inventario Release. El EXE y la DLL coinciden con la compilación Release validada.

La comprobación se realizó en el equipo de desarrollo Windows 11, abriendo
`Iniciar-demo.cmd` en la copia del paquete del 19 de noviembre. Con el servidor
de desarrollo activo, se utilizó `http://localhost:8767/`:

| Paso | Resultado confirmado |
| --- | --- |
| Arranque mediante el lanzador | Modo demostración con la política Libre. |
| PDF permitido bajo Restrictiva | Guardado con el texto fijo y aviso de descarga completada. |
| EXE sugerido bajo Restrictiva | Aviso de extensión bloqueada, sin diálogo ni archivo final. |
| PDF renombrado a `zip-renombrado.exe` | Bloqueo del nombre definitivo y ausencia del archivo final. |
| Cancelación voluntaria tras recargar | Diálogo cerrado, sin archivo nuevo ni aviso nuevo de bloqueo o error. |
| Respuesta incompleta | Aviso de error de descarga. |
| Recarga tras el error | Retirada del aviso anterior. |
| Cierre mediante la X | Bucle Qt con código 0 y apagado de CEF completado. |

El registro `desktop-1795093212144-27400.log`, bajo
`%LOCALAPPDATA%\SecureBrowser\Logs`, confirma Release, arranque en Libre,
cambio a Restrictiva y cierre normal. Los resultados del diálogo y de los avisos
fueron comprobados por el usuario. Esta pasada no sustituye una validación en
una máquina limpia ni repite en el ZIP los casos de `blob:` y `data:` registrados
anteriormente para Debug y Release.
