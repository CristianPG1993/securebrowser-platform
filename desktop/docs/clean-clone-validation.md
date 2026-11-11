# Validación de la guía desde un clon limpio

Registro de la [tarea #95](https://github.com/CristianPG1993/securebrowser-platform/issues/95).
La secuencia que se debe seguir está en el [README de Desktop](../README.md).

**Estado: configuración, compilación y prueba visual local verificadas con resultados correctos en Debug y Release.**
Se han ejecutado la preparación y las pruebas locales descritas abajo.
Las verificaciones automáticas previas de CI se identifican por separado.

## Evidencia anterior disponible

| Comprobación anterior | Evidencia y alcance |
| --- | --- |
| Verificaciones y matriz visual de #93 | [Resultados registrados](../tests/README.md#resultados-registrados-para-93) y [matriz manual](../tests/README.md#matriz-visual-manual), confirmados en Debug y Release. |
| CI de #94 con preparación limpia | [Primer intento correcto de la PR #112](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/42356726305/job/128356332501), sobre `32146ce4a47cb8c5b9174b7971667087f0e84c15`. Compilación y comprobaciones Debug/Release en Windows Server 2022. |
| CI de #94 con caché | [Segundo intento del mismo commit](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/42356726305/job/128358134667); restauración de la misma clave y reutilización de CEF confirmadas, con verificaciones correctas. |
| Desktop después del merge de #94 | [Desktop CI de main](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/42359211806), correcto sobre `47d2ea078e009ae930377447137e7926ea6510f9`. |
| Backend después del merge de #94 | [Backend CI de main](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/42359211824), correcto sobre la misma revisión. |

Esas ejecuciones no acreditan haber seguido esta nueva guía en Windows 11.
CI comprueba la ventana hija nativa y los logs, sin verificar visualmente el
contenido de la página ni los controles. Los resultados históricos de etapas
anteriores se conservan en las [notas técnicas](implementation-notes.md).

## Identificación del nuevo intento

Se recogen los datos comprobados y los metadatos de la configuración
generada en el clon. No se capturaron las versiones exactas de PowerShell y Git
durante la prueba manual.

| Dato | Valor observado |
| --- | --- |
| Fecha de ejecución | Comprobación realizada el 8 de noviembre de 2026. |
| Rama y SHA del commit comprobado | `docs/desktop-build-run`, `73be7b20303419de37233e46004d56ccf28c608c`, confirmados al clonar. |
| Windows, versión y arquitectura | Windows 11 Home, `10.0.26300`, 64 bits, consultado mediante `Win32_OperatingSystem`. |
| PowerShell, Git y Python | Python `3.13.3`, leído de `pyvenv.cfg` del entorno aqt del clon. Versiones exactas de PowerShell y Git no registradas. |
| Visual Studio Build Tools y MSVC v143 | MSVC `19.44.35223.0`, arquitectura `x64`, según `CMakeCXXCompiler.cmake` generado en el clon; configuración con toolset `v143`. |
| Windows SDK, paquete y target | Preparación confirmada con código 0: paquete `10.0.28000.2270`, target `10.0.28000.0`. |
| CMake | Selección de `3.29.2` y código 0 confirmados. |
| Qt y kit | Preparación de Qt `6.12.0`, kit `msvc2022_64`, verificada. |
| CEF, distribución y arquitectura | Preparación correcta confirmada con el script compartido: `156.0.14+g4fa4ab5+chromium-156.0.8078.48`, Standard `windows64`. |
| 7-Zip | `26.06`, utilizado para preparar CEF mediante la ruta local. |
| Clon nuevo y sin salidas de otro directorio | Clonación y estado inicial limpio confirmados; las compilaciones se han realizado en `desktop/build/` de esa copia. |
| Ruta de Qt usada: descarga nueva o kit existente verificado | Preparación en la ruta predeterminada del clon: `desktop/.deps/qt/6.12.0/msvc2022_64`. |

Desde la raíz del clon, en la terminal donde ya se ha preparado CMake:

```powershell
git rev-parse HEAD
git status --short
$PSVersionTable.PSVersion.ToString()
git --version
python --version
cmake --version
```

Anotar también las versiones que muestran los scripts y la configuración
CMake. El clon debe empezar sin `desktop/build/` ni un SDK copiado de otro
clon dentro de `desktop/.deps/`. La instalación del compilador y del SDK de
Windows es un requisito del equipo, independiente de la copia de fuentes.

## Resultados de la secuencia

Seguir el README en orden. Registrar el resultado real y el código de salida;
si falla, conservar el diagnóstico y corregirlo antes de continuar.
Para esta validación se aprovecha la evidencia de CI de las comprobaciones
automáticas. La ejecución local se centra en preparar el clon, configurar,
compilar y revisar visualmente Debug y Release. Los resultados automáticos
anteriores no se presentan como pruebas nuevas ejecutadas en este clon.

| Paso de la guía | Resultado observado |
| --- | --- |
| Clonación y estado inicial limpio | SHA y ausencia de cambios comprobados. |
| Preparación o comprobación de Windows SDK, CMake y 7-Zip | SDK y CMake correctos; 7-Zip `26.06` utilizado en la preparación de CEF. |
| Preparación de Qt y variable del kit | Preparación confirmada tras ejecutar el bloque de Qt. |
| Preparación de CEF y `CEF_ROOT` | Preparación correcta verificada con el script compartido y 7-Zip `26.06`. Los intentos anteriores se describen más abajo. |
| Configuración de CMake con las dependencias elegidas | Resultado correcto verificado tras ejecutar el bloque de configuración y construcción. |
| Compilación y despliegue Debug | Compilación correcta y ejecución visual posterior verificadas. |
| Compilación y despliegue Release | Compilación correcta y ejecución visual posterior verificadas. |
| Comprobación de rechazo de Qt ausente | No repetida localmente; evidencia previa en CI de #94. |
| Inventario, arranque y cierre automáticos Debug | No repetidos localmente; evidencia previa en CI de #94. |
| Inventario, arranque y cierre automáticos Release | No repetidos localmente; evidencia previa en CI de #94. |

## Incidencia detectada al preparar CEF

El primer intento con `desktop/ci/setup-cef.ps1` falló porque no existía
`7-Zip/7z.exe` bajo `ProgramFiles`. Tras instalar 7-Zip, el segundo devolvió
`Se requiere 7-Zip 26.05; encontrado: 26.06.`. En ambos casos el wrapper
terminó antes de preparar el SDK. El fallo posterior de `Resolve-Path` se
debió a que el directorio CEF todavía no existía.

La guía se ha ajustado para comprobar primero el extractor y utilizar en
local `scripts/setup-cef.ps1` con `-ArchiveExtractor SevenZip` y la ruta
explícita de `7z.exe`. La versión exacta del wrapper de CI se conserva.
Se comprobó que el nuevo intento terminó correctamente y mostró
`CEF_ROOT`. La prueba se inició en `73be7b2`; las correcciones posteriores
de la guía son documentación.

## Comprobaciones visuales del nuevo clon

Se ejecutó el bloque de apertura y cierre en Debug y Release y se comprobó
el funcionamiento de todas las comprobaciones solicitadas, con una instancia
cada vez. Esta pasada breve corresponde a las áreas siguientes de la
[matriz manual](../tests/README.md#matriz-visual-manual).

| Área | Debug | Release |
| --- | --- | --- |
| Renderizado HTTPS dentro de Qt | Correcto. | Correcto. |
| Barra de direcciones, Atrás, Adelante y Recargar | Correcto. | Correcto. |
| Foco, edición y Alt+Tab | Correcto. | Correcto. |
| Tamaño, minimizar y restaurar | Correcto. | Correcto. |
| Cierre con salida 0 y sin procesos tras cinco segundos | Correcto. | Correcto. |
| Log propio: creación, cierre y `CEF shutdown completed.` | No vuelto a inspeccionar en esta pasada; evidencia previa en CI de #94. | No vuelto a inspeccionar en esta pasada; evidencia previa en CI de #94. |

La confirmación manual incluye la salida 0 y la ausencia de procesos tras
el margen de cinco segundos, con ninguna otra instancia abierta. No se han
adjuntado logs nuevos ni se presentan los logs anteriores como resultado de
esta ejecución. Los casos negativos y diálogos adicionales conservan su
evidencia histórica; no se han repetido en esta pasada.

## Cierre de la validación

La comprobación local de la guía queda registrada con la corrección del uso
de 7-Zip y los límites indicados. La ejecución se realizó sobre `73be7b2`;
los cambios posteriores de esta tarea corrigen documentación y registran los
resultados, sin modificar el código de aplicación ni los scripts.
La PR debe incluir su relación con #95 y los checks correctos de la última
revisión antes del merge manual. Después se comprueban también los workflows
del commit integrado en `main`.

Este registro no cierra por sí solo la aceptación global de
[#76](https://github.com/CristianPG1993/securebrowser-platform/issues/76).
