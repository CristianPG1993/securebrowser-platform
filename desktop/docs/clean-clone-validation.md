# Validación de la guía desde un clon limpio

Registro de la [tarea #95](https://github.com/CristianPG1993/securebrowser-platform/issues/95).
La secuencia que se debe seguir está en el [README de Desktop](../README.md).

**Estado: pendiente de ejecución por el autor en Windows 11 x64.**
La revisión de la documentación y sus ejemplos no constituye una ejecución
de configuración, compilación o pruebas desde el nuevo clon.

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

Completar con valores observados, después de preparar y ejecutar el clon.
No publicar rutas personales ni copiar secretos o perfiles al registro.

| Dato | Valor observado |
| --- | --- |
| Fecha de ejecución | Pendiente. |
| Rama y SHA del commit comprobado | Pendiente. |
| Windows, versión y arquitectura | Pendiente. |
| PowerShell, Git y Python | Pendiente. |
| Visual Studio Build Tools y MSVC v143 | Pendiente. |
| Windows SDK, paquete y target | Pendiente. |
| CMake | Pendiente. |
| Qt y kit | Pendiente. |
| CEF, distribución y arquitectura | Pendiente. |
| 7-Zip | Pendiente. |
| Clon nuevo y sin salidas de otro directorio | Pendiente. |
| Ruta de Qt usada: descarga nueva o kit existente verificado | Pendiente. |

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

| Paso de la guía | Resultado observado |
| --- | --- |
| Clonación y estado inicial limpio | Pendiente. |
| Preparación o comprobación de Windows SDK, CMake y 7-Zip | Pendiente. |
| Preparación de Qt y variable del kit | Pendiente. |
| Preparación de CEF y `CEF_ROOT` | Pendiente. |
| Configuración de CMake con las dependencias elegidas | Pendiente. |
| Compilación y despliegue Debug | Pendiente. |
| Compilación y despliegue Release | Pendiente. |
| Comprobación de rechazo de Qt ausente | Pendiente. |
| Inventario, arranque y cierre automáticos Debug | Pendiente. |
| Inventario, arranque y cierre automáticos Release | Pendiente. |

## Comprobaciones visuales del nuevo clon

Repetir la [matriz manual](../tests/README.md#matriz-visual-manual) con una
configuración cada vez y registrar aquí los resultados nuevos.

| Área | Debug | Release |
| --- | --- | --- |
| Renderizado HTTPS dentro de Qt | Pendiente. | Pendiente. |
| Barra de direcciones, Atrás, Adelante y Recargar | Pendiente. | Pendiente. |
| Foco, edición y Alt+Tab | Pendiente. | Pendiente. |
| Tamaño, minimizar/restaurar y maximizar | Pendiente. | Pendiente. |
| Cierre con salida 0 y sin procesos tras cinco segundos | Pendiente. | Pendiente. |
| Log propio: creación, cierre y `CEF shutdown completed.` | Pendiente. | Pendiente. |

Identificar los logs por configuración y PID y conservar los archivos locales.
Si se adjuntan extractos, indicar de qué intento proceden. La ausencia de
procesos se comprueba con ninguna otra instancia abierta.

## Cierre de la validación

Antes de dar por validada #95, completar este registro con la ejecución real,
anotar incidencias y límites y revisar los enlaces y comandos de la guía.
La PR debe incluir el diff de documentación, su relación con #95 y los checks
correctos de la última revisión. El merge es manual; después se comprueban
también los workflows del commit integrado en `main`.

Este registro no cierra por sí solo la aceptación global de
[#76](https://github.com/CristianPG1993/securebrowser-platform/issues/76).
