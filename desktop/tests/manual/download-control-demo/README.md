# Comprobación manual de Download Control y Qt

Procedimiento para el EXE real y las políticas locales de la tarea #119.
Complementa las pruebas automatizadas con la interacción del diálogo y los
avisos de Qt. Las comprobaciones realizadas en Debug se recogen al final;
la validación de Release con los últimos avisos sigue pendiente.

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
de prueba se comprobó con 29 bytes y el texto esperado. La validación anterior
de la batería automática consta de 196 casos por configuración; no se presenta
como una repetición de la regresión después de añadir estos avisos.
Quedan pendientes Release y la regresión de la revisión final.
