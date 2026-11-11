# CI de Desktop

El workflow [Desktop CI](../../.github/workflows/desktop.yml) cubre Desktop Foundation de la
[tarea #94](https://github.com/CristianPG1993/securebrowser-platform/issues/94)
y las pruebas del Security Engine de la
[tarea #115](https://github.com/CristianPG1993/securebrowser-platform/issues/115).
Se ejecuta en PR hacia `main`, con los pushes a `main` y mediante ejecución
manual. El job se llama `Desktop build and tests` y utiliza `windows-2022`.

## Preparación y comprobaciones

[toolchain.json](toolchain.json) fija las herramientas y el kit Qt; CEF conserva
su [manifiesto propio](../cef-sdk.json), ICU utiliza
[icu-sdk.json](../icu-sdk.json) y GoogleTest se fija en
[googletest.json](../googletest.json). Los scripts utilizan sus rutas
predeterminadas dentro de `desktop/.deps`, sin rutas específicas de un equipo ni
credenciales de descarga.

aqtinstall se fija por commit completo en `qt.aqtRevision`. La versión 3.3.0
publicada no reconoce la nueva ruta del repositorio Windows de Qt 6.11 y
posteriores; la [revisión seleccionada](https://github.com/miurahr/aqtinstall/commit/7e5a5c3d95cf962cfc2f36c86ffa0d2c07f1a0d4)
incorpora ese soporte. Su instalación desde Git requiere el Git y Python
disponibles en el runner.

Los scripts seleccionan el primer ejecutable de PATH cuando `Get-Command`
encuentra varias instalaciones de una herramienta. Esto evita combinar, por
ejemplo, las rutas de los `tar.exe` de Windows y de Git en una sola llamada.

| Paso | Implementación |
| --- | --- |
| Preparar y seleccionar CMake | [setup-cmake.ps1](setup-cmake.ps1); verifica la versión y el SHA-256 del ZIP antes de extraer una descarga. |
| Preparar Windows SDK | [setup-windows-sdk.ps1](setup-windows-sdk.ps1); comprueba el paquete registrado y sus componentes x64, o verifica hash y firma del instalador antes de ejecutarlo. |
| Preparar Qt | [setup-qt.ps1](setup-qt.ps1); usa aqtinstall en un entorno Python aislado, mantiene sus comprobaciones de hashes y revisa los archivos del kit. |
| Preparar CEF | [setup-cef.ps1](setup-cef.ps1); selecciona el 7-Zip fijado para el runner y llama al [módulo compartido](../../scripts/setup-cef.ps1), que verifica o prepara el SDK. |
| Preparar ICU | [setup-icu.ps1](../../scripts/setup-icu.ps1); verifica tamaño y SHA-256 del paquete y comprueba archivos y versiones de ICU y Unicode antes de configurar Desktop. |
| Preparar GoogleTest | [setup-googletest.ps1](../../scripts/setup-googletest.ps1); verifica tamaño y SHA-256 del archivo y comprueba las fuentes y la versión fijadas en el manifiesto. |
| Configurar el núcleo y sus pruebas | Paso `Configure security core tests`; utiliza el generador, toolset y SDK del manifiesto en `desktop/build/tests`, con `SBP_BUILD_DESKTOP_APP=OFF` y `BUILD_TESTING=ON`. |
| Compilar y ejecutar las pruebas del núcleo | Paso `Build and run security core tests`; construye `sbp_security_tests` y ejecuta CTest en Debug y Release, con `--no-tests=error` y un timeout de 60 segundos por caso. |
| Configurar CMake | [configure.ps1](configure.ps1); selecciona los SDK, las configuraciones y los avisos de Qt conservados en el repositorio. |
| Comprobar rechazo de Qt ausente | [check-configuration-rejection.ps1](../tests/check-configuration-rejection.ps1). |
| Compilar | `cmake --build desktop/build --config <configuración> --parallel 4`, primero Debug y después Release. |
| Comprobar las dos salidas | [check-runtime.ps1](../tests/check-runtime.ps1) y [check-startup.ps1](../tests/check-startup.ps1), de forma secuencial. |

Los programas externos deben devolver el código esperado. Un fallo de
compilación o verificación detiene el job. Si la aplicación ha generado logs,
el paso de diagnóstico muestra sus últimas líneas cuando falla el workflow.

La extracción de CEF en CI utiliza 7-Zip en dos pasos: descomprime la capa BZip2
y extrae el TAR resultante. Cada paso comprueba su código de salida; después se
validan los archivos, la versión y la arquitectura del SDK antes de publicarlo
en `.deps/cef`. El paso completo tiene un límite de diez minutos. El script
compartido mantiene `tar` como opción predeterminada para las ejecuciones locales.

ICU se prepara con el mismo script compartido que se utiliza localmente y
un límite de cinco minutos. El SDK fijado es ICU4C `76.1`, Unicode `16.0`,
Windows x64 para MSVC 2022. CMake copia sus DLL y avisos junto a la aplicación;
el inventario de runtime exige también esos archivos.

GoogleTest se prepara con el script compartido y un límite de cinco minutos.
Las fuentes se reutilizan para ambas configuraciones y se compilan como parte del
ejecutable de pruebas. La configuración del núcleo tiene un límite de cinco minutos
y el paso conjunto de compilación y ejecución, de quince minutos.
La configuración aislada termina antes de buscar Qt o CEF. Las DLL y los avisos
de ICU se copian junto a `sbp_security_tests.exe` en cada configuración.

## Construcción limpia y caché

La caché incluye los SDK preparados de CEF, Qt e ICU, las fuentes preparadas de
GoogleTest y la copia portátil de CMake.
Las dependencias se comparten entre ambas compilaciones; CEF y Qt incluyen
sus variantes Debug/Release y el paquete ICU utiliza las mismas DLL mediante su API C.
No se cachean `desktop/build`, los perfiles CEF ni los diagnósticos de ejecución.
Por tanto, cada job configura y compila las fuentes de nuevo.

La clave incluye runner, arquitectura, las dos configuraciones y un hash de los
manifiestos y scripts de preparación. No se utilizan claves de restauración
alternativas. La caché se guarda después de superar la preparación de las
dependencias, antes de compilar el proyecto.
La clave incluye también `desktop/icu-sdk.json`, `scripts/setup-icu.ps1`,
`desktop/googletest.json` y `scripts/setup-googletest.ps1`;
un cambio en cualquiera de ellos evita reutilizar una caché anterior incompatible.

Para acreditar la preparación inicial y la reutilización:

1. Abrir la ejecución de la PR y comprobar que `Restore dependency cache`
   informa de que no encuentra la clave. Deben completarse todos los pasos.
2. Repetir el job de Desktop sobre la misma revisión desde Actions.
3. Comprobar que se restaura la misma clave, que los scripts reutilizan las
   dependencias y que la compilación y verificaciones vuelven a pasar.
4. Registrar los enlaces a ambos intentos en la PR.

Para repetir una ejecución existente, abrir **Actions → Desktop CI**, entrar en
el intento y seleccionar **Re-run jobs → Re-run all jobs**. Para repetir solo
los trabajos fallidos, seleccionar **Re-run failed jobs**. El reintento utiliza
el mismo commit original; los nuevos commits enviados a una PR generan otra
ejecución con el código actualizado.

Cuando el workflow esté en `main`, el botón **Run workflow** permite desmarcar
`use_cache` para preparar las dependencias desde cero sin restaurar ni guardar
caché. La primera ejecución de una PR también prepara las dependencias desde
cero si su clave todavía no existe.

La primera ejecución en `main` puede necesitar preparar su propia caché:
GitHub no permite restaurar desde `main` una caché creada exclusivamente para
una PR. La preparación se repite cuando cambia la clave o la caché deja de
estar disponible. El motor CEF ya está precompilado en el SDK; cada ejecución
compila la aplicación SecureBrowser y el adaptador `libcef_dll_wrapper`, además de
realizar las comprobaciones Debug y Release.

El workflow utiliza el runner estándar y no amplía los límites de caché del
repositorio ni publica artefactos de distribución.

## Alcance de las pruebas

Las pruebas del núcleo comprueban validación, contexto, publicación y lectura de
estados inmutables, vigencia con reloj controlado y recorridos concurrentes.
No crean una aplicación Qt ni inicializan CEF; su cobertura y los resultados
locales registrados se detallan en la [guía de pruebas](../tests/README.md).
La ejecución de este bloque en GitHub Actions queda pendiente hasta publicar
la revisión del workflow y comprobar sus resultados.

La prueba de arranque acredita la aparición de la ventana hija nativa de CEF,
el cierre normal, la salida 0 y las marcas de creación y apagado del mismo
navegador. No comprueba el contenido renderizado ni navega mediante los
controles de Qt.

Renderizado, navegación, foco, cambio de tamaño y comprobación visual del
cierre requieren la [revisión manual en Windows 11](../tests/README.md), tanto
en Debug como en Release.

Antes del merge manual, revisar los checks de la última revisión de la PR,
incluido `Backend build and tests`. Después del merge, comprobar también la
ejecución correspondiente al push a `main`. Este workflow no modifica las
reglas de protección del repositorio.
