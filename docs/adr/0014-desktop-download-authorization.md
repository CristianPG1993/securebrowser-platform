# ADR-0014: Selección de destino y autorización de descargas en Desktop

- Estado: Aceptada
- Fecha: 2026-11-08

## Contexto

Download Control debe comprobar el nombre que finalmente se utilizará al guardar.
Evaluar solo el nombre sugerido por CEF no cubre un cambio de extensión realizado
por el usuario en «Guardar como».

La política también puede cambiar o caducar mientras el diálogo está abierto
o después de autorizar una transferencia, y el backend puede retirar la
autorización de la instalación. Este ADR concreta el tratamiento de las descargas,
complementando la disponibilidad y vigencia del ADR-0012.

## Opciones consideradas

| Aspecto | Alternativas y consecuencias |
| --- | --- |
| Selección del destino | Mantener el nombre y elegir solo la carpeta simplifica el control, pero impide renombrar. Un «Guardar como» con nombre editable ofrece el flujo habitual y exige comprobar el nombre definitivo. |
| Política al confirmar | Conservar la política inicial mantiene estable la decisión durante el diálogo, pero puede utilizar reglas sustituidas o caducadas. Consultar el snapshot local vigente respeta las restricciones del momento y puede bloquear una solicitud inicialmente permitida. |
| Transferencias autorizadas | Permitir que terminen mantiene la autorización concedida y simplifica el MVP, pero pueden finalizar después de cambiar las restricciones. Reevaluarlas permite cancelar si dejan de estar permitidas, exige seguimiento durante la transferencia y no revierte los datos recibidos. |
| Nombres inválidos | Corregir automáticamente un nombre puede cambiar el archivo evaluado. Rechazarlo conserva el nombre como entrada explícita y permite mostrar el motivo. |
| Denegación de la instalación | Conservar la autorización hasta agotar la vigencia permitiría continuar tras una denegación explícita. Retirarla inmediatamente exige cancelar también las transferencias pendientes y activas. |

## Decisión

En la tarea #114 se adoptaron las siguientes decisiones.

### Selección y comprobación del destino

1. Validar y evaluar el nombre sugerido por CEF. Si está bloqueado, cancelar la descarga
   y mostrar el motivo sin abrir el diálogo de guardado.
2. Si está permitido, mostrar un diálogo estándar de Qt de «Guardar como», con
   ubicación y nombre editables.
3. Al confirmar, comprobar que existe una política local válida y vigente para
   la misma sesión e instalación que inició la solicitud, y evaluar el nombre
   definitivo, después de validarlo, con la política aplicada en ese momento.
4. Continuar únicamente si esa evaluación lo permite, mediante
   `Continue(ruta, false)`, con la ruta elegida y evaluada.

No se delegará después la elección del nombre a otro diálogo de CEF. Un nombre
definitivo bloqueado cancelará la descarga y se mostrará el motivo.
Cancelar voluntariamente «Guardar como» terminará la solicitud sin un aviso de
bloqueo de política ni un evento `DOWNLOAD_BLOCKED`.

### Validez del nombre y activación del control

Se validarán tanto el nombre sugerido como el definitivo antes de evaluar sus
extensiones. Se rechazarán nombres vacíos, `.` y `..`, puntos o espacios finales,
caracteres de control o reservados de Windows y nombres reservados de dispositivos,
incluso con extensión, como `NUL.txt`. No se corregirán silenciosamente para
permitir la descarga. Un nombre como `informe.pdf:datos` se rechazará por `:`.

La carpeta y el nombre se tratarán por separado, conservando los nombres Unicode
válidos. Las restricciones del nombre no se aplicarán indiscriminadamente a toda
la ruta de guardado.

Con `downloadControlEnabled = true`, se evaluará la última extensión en minúsculas
Unicode y sin punto, con conversión independiente del idioma de Windows y
compatible con el backend. `informe.pdf.exe` se evaluará por `exe`. Los nombres
`informe` y `.env` se considerarán sin extensión y se bloquearán por ese motivo
antes de consultar las listas.

Con `downloadControlEnabled = false`, se omitirán las listas y el bloqueo por
ausencia de extensión. Se mantendrán la validez del nombre y los requisitos de
política vigente y autorización. Esta decisión concreta el requisito inicial:
la ausencia de extensión provoca bloqueo por Download Control solo si está activo.

### Política vigente al confirmar

Si se aplicó una política nueva mientras el diálogo estaba abierto, la evaluación
final utilizará sus reglas. Si la política caducó, no es válida o terminó la
sesión, se cancelará la solicitud y se mostrará el motivo, sin llamar a `Continue`.
Una denegación explícita de la instalación también impedirá continuar.
La solicitud no se trasladará a otra sesión o instalación.

Se utilizará el snapshot local vigente, sin exigir una consulta REST por cada
confirmación.

### Descargas autorizadas y en curso

Una transferencia ya autorizada mediante `Continue` podrá terminar durante la
misma sesión e instalación mientras sigan autorizadas, aunque después cambie
o caduque la política. En el MVP no se reevaluará por esos cambios.

Las nuevas solicitudes y las pendientes de «Guardar como» seguirán sujetas a
la política local vigente antes de autorizar su continuación. Terminar la sesión,
cerrar el navegador o recibir una denegación explícita de la instalación cancelará
las descargas pendientes y activas.

La denegación explícita corresponde a `403`, `404` o `409 DEVICE_INACTIVE` de la
consulta de política del dispositivo actual, interpretados por REST/Auth para
el contexto que inició la consulta. No se esperará a que venza la política.
Los fallos temporales, los errores de otras operaciones y un `401` por sí solo
no se tratarán como esa denegación. Si REST/Auth confirma que la sesión no puede
recuperarse, se aplicará la cancelación por fin de sesión.

## Consecuencias

- Cambiar de un nombre permitido a otro con extensión bloqueada no permitirá
  continuar la descarga con ese nombre definitivo.
- Habrá que coordinar el diálogo Qt con la continuación de CEF y distinguir
  cancelación voluntaria, bloqueo de política y finalización de sesión.
- Una política nueva puede bloquear una solicitud que ya tenía abierto el diálogo.
- Una descarga autorizada puede terminar después de cambiar las restricciones;
  el MVP aplica el control antes de continuar, sin reevaluación durante la transferencia.
- Será necesario gestionar las solicitudes pendientes y transferencias activas
  para cancelarlas al terminar la sesión, cerrar el navegador o retirarse
  explícitamente la autorización de la instalación.
- Desactivar Download Control permitirá nombres válidos sin extensión, pero
  mantendrá las comprobaciones generales de autorización y guardado.
- Las comprobaciones conservarán los nombres Unicode válidos y rechazarán los
  inválidos sin transformar automáticamente el destino que se va a evaluar.

## Validación

La implementación y las pruebas están pendientes. Se comprobará que:

- Un nombre sugerido bloqueado no abre «Guardar como».
- Cambiar el nombre definitivo a una extensión bloqueada cancela la solicitud.
- Los nombres sugeridos y definitivos inválidos se rechazan también con el
  control desactivado, incluidos nombres reservados, puntos o espacios finales
  y caracteres no admitidos, sin corregirlos silenciosamente.
- La extracción utiliza la última extensión; `informe` y `.env` se consideran
  sin extensión y solo se bloquean por esa ausencia con el control activo.
- Las comprobaciones separan carpeta y nombre y conservan los nombres Unicode
  válidos, con normalización de extensiones compatible con el backend.
- CEF recibe la ruta evaluada y no abre otro diálogo para cambiarla.
- Un cambio de política durante el diálogo gobierna la evaluación final;
  caducidad, política inválida o fin de sesión impiden llamar a `Continue`.
- Cancelar voluntariamente el diálogo no genera `DOWNLOAD_BLOCKED` ni un aviso
  de bloqueo de política.
- Una transferencia autorizada puede terminar aunque cambie o caduque la política,
  mientras la sesión e instalación sigan autorizadas y las nuevas solicitudes
  y las pendientes respeten el control vigente.
- Terminar la sesión o cerrar el navegador cancela pendientes y activas sin
  trasladarlas a otro contexto.
- Una denegación explícita de la instalación cancela pendientes y activas antes
  de que venza la política; no se confunde con fallos temporales, errores de
  otras operaciones ni resultados de consultas de un contexto anterior.
- Las pruebas con CEF acreditan tanto la cancelación como una descarga permitida
  completada; los tests del evaluador por sí solos no acreditan esos resultados.

## Referencias

- [Tarea #114: contratos, límites y decisiones del MVP](https://github.com/CristianPG1993/securebrowser-platform/issues/114).
- [Diseño del Security Engine](../architecture/security-engine.md).
- [ADR-0011: integración CEF](0011-desktop-cef-integration.md).
- [ADR-0012: disponibilidad y vigencia local de la política](0012-desktop-policy-availability.md).
- [ADR-0015: producción y entrega de eventos](0015-desktop-security-event-production.md).
- [Contrato de descarga de CEF fijado](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/include/cef_download_handler.h).
- [Nombres de archivos y rutas en Windows](https://learn.microsoft.com/en-us/windows/win32/fileio/naming-a-file).
