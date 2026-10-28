# SecureBrowser Desktop

## Estado actual

`SecureBrowser.exe` abre una ventana Qt con el título **SecureBrowser** y un
tamaño inicial de 1024 por 768 unidades lógicas, mientras CEF (Chromium Embedded
Framework) permanece inicializado. `QApplication` y los widgets viven en el hilo principal;
CEF procesa sus eventos en su propio hilo de interfaz. Al cerrar la última
ventana, termina el bucle Qt, se destruye la ventana y se apaga CEF antes de
destruir `QApplication`.
El target `SecureBrowser` construye nuestra DLL (Dynamic-Link Library,
biblioteca de enlace dinámico), enlazada con Qt y CEF. Junto a ella copia el
bootstrap oficial como `SecureBrowser.exe`.
La construcción prepara también los binarios y recursos CEF, las dependencias
Qt, los avisos de terceros y los permisos de la carpeta de ejecución.
La entrada `RunWinMain` de [`app_entry_win.cpp`](src/app/app_entry_win.cpp) distingue
el proceso principal de los subprocesos antes de crear objetos Qt. En el
principal comprueba el contexto de sandbox y crea `QApplication` antes de
inicializar CEF. Tras una inicialización correcta, muestra el `QWidget` y
ejecuta `qt_app.exec()`. Todavía no crea instancias `CefBrowser` ni carga
contenido web. `sbp_toolchain_check` conserva una ventana Qt independiente
como comprobación básica del kit.
`BrowserApp` recibe el aviso de contexto preparado y `BrowserClient` contiene
los handlers (receptores de avisos) de creación y cierre para el navegador
que incorporaremos en #88. Ambos objetos utilizan referencias de CEF.
La preparación independiente del SDK CEF está disponible mediante
[`setup-cef.ps1`](../scripts/setup-cef.ps1).

## Organización del código

El código de la aplicación se agrupa dentro de `src/`, conservando cada header
junto a su implementación:

| Carpeta | Responsabilidad | Archivos actuales |
| --- | --- | --- |
| `src/app/` | Arranque y coordinación de Qt y CEF. | `app_entry_win.cpp` |
| `src/browser/` | Objetos propios que atienden los avisos de CEF. | `browser_app.h`, `browser_client.h` y `browser_client.cpp` |
| `src/ui/` | Ventana y comportamiento de la interfaz Qt. | `browser_window.h` y `browser_window.cpp` |

`CMakeLists.txt` incorpora esos archivos al target `SecureBrowser` y utiliza
`src/` como raíz de los includes propios. `main.cpp` sigue siendo la
comprobación independiente del kit Qt (`sbp_toolchain_check`). Las carpetas de
políticas, Security Engine, persistencia y comunicación REST se añadirán al
incorporar sus primeras clases.

## Requisitos

- Windows 11 x64.
- CMake 3.29 o superior; versión de referencia: 3.29.2.
- Visual Studio Build Tools 2022 con las herramientas C++ MSVC
  (Microsoft Visual C++) v143 para x64.
- Windows SDK (Software Development Kit, kit de desarrollo de software),
  paquete `10.0.28000.2270`. El identificador de instalación utilizado por
  CMake es `10.0.28000.0`.
- Qt 6.12.0, kit `msvc2022_64`, con las bibliotecas Debug y Release.
- Textos `Licenses/` del instalador Qt e inventarios `sbom/` del kit.
- Runtime de Microsoft Visual C++ v143 x64 compatible para ejecutar Release.
  Debug requiere las bibliotecas de depuración del entorno de desarrollo.
- Para preparar CEF: Windows PowerShell 5.1 y `tar.exe`, incluido en Windows 11.
  La primera descarga requiere conexión a Internet.

Las decisiones de integración están recogidas en el
[ADR-0011 (Architecture Decision Record, registro de decisión arquitectónica)](../docs/adr/0011-desktop-cef-integration.md).

## Preparar Qt

1. Obtener Qt Online Installer para Windows x64 desde las
   [descargas oficiales](https://download.qt.io/official_releases/online_installers/).
2. Iniciar sesión con una cuenta Qt y elegir **Custom Installation**
   (instalación personalizada), siguiendo la
   [guía oficial](https://doc.qt.io/qt-6/qt-online-installation.html).
3. Seleccionar **Qt 6.12.0 > MSVC 2022 64-bit** y completar la instalación.
   El kit incluye Qt Widgets. El paquete adicional de símbolos
   **Qt Debug Information Files** no es necesario para compilar en Debug.

Instalar fuera del repositorio. En los ejemplos se utiliza `C:/Qt` como raíz;
si se elige otra carpeta, sustituirla en los comandos. La ruta del kit será
`C:/Qt/6.12.0/msvc2022_64` con esa raíz.

Para consultar la versión instalada desde PowerShell:

```powershell
& "C:/Qt/6.12.0/msvc2022_64/bin/qmake.exe" -query QT_VERSION
```

El resultado esperado es `6.12.0`. Se usa la ruta completa para seleccionar
este kit aunque exista otra versión de Qt en el `PATH` (variable de entorno
de búsqueda de ejecutables).

## Preparar CEF

El manifiesto [`cef-sdk.json`](cef-sdk.json) fija la versión
`156.0.14+g4fa4ab5+chromium-156.0.8078.48`, distribución Standard `windows64`,
junto con el archivo, la URL (Uniform Resource Locator), el tamaño y el
SHA-1 (Secure Hash Algorithm 1) publicados. La preparación usa esos valores;
no selecciona automáticamente otra versión.

Desde PowerShell, en la raíz del repositorio:

```powershell
.\scripts\setup-cef.ps1
```

En una primera preparación, el script descarga aproximadamente 367 MB,
comprueba el tamaño y el SHA-1 del archivo y extrae el paquete completo.
Utiliza ubicaciones temporales durante la descarga y extracción; el SDK
pasa a su ruta definitiva después de comprobar los archivos esenciales,
la versión de `include/cef_version.h` y la arquitectura x64 del bootstrap
y `libcef.dll` de Debug y Release.

Las rutas predeterminadas son:

| Ruta | Contenido |
| --- | --- |
| `desktop/.deps/downloads/` | Archivo comprimido verificado. |
| `desktop/.deps/cef/<directorio-del-paquete>/` | SDK completo: cabeceras, módulos CMake, fuentes del adaptador, binarios, recursos y avisos. |

Para repetir la preparación, ejecutar el mismo comando. Si el SDK existente
supera las comprobaciones de archivos esenciales, versión y arquitectura,
se reutiliza sin descargar ni extraer. Si solo existe el archivo comprimido,
se comprueba su tamaño y SHA-1 antes de extraerlo. La reutilización del SDK
no recalcula un hash de cada archivo extraído.

Para elegir otra ubicación, por ejemplo fuera del repositorio:

```powershell
.\scripts\setup-cef.ps1 -DependenciesDirectory "C:/SBP-deps"
```

El directorio indicado contendrá las subcarpetas `downloads/` y `cef/`.
Las rutas relativas proporcionadas por el usuario se resuelven desde la
carpeta actual de PowerShell. La ubicación predeterminada y la del manifiesto
se calculan desde la carpeta del script, por lo que no dependen de esa carpeta
actual.

Al terminar se muestra `CEF_ROOT: <ruta-completa-del-SDK>`. Esa ruta se
proporciona a CMake como se explica a continuación. La preparación se ejecuta
explícitamente, separada de la configuración CMake.

Si el manifiesto es inválido, el archivo comprimido tiene un tamaño o SHA-1
incorrecto, o el SDK existente está incompleto o no corresponde a la versión
y arquitectura requeridas, el script termina con un diagnóstico. Los archivos
incorrectos ya presentes en la caché y los SDK existentes se conservan para
revisión. Corregir o apartar el elemento indicado antes de repetir el comando.
Los temporales creados por el intento se limpian al finalizar.

`desktop/.deps/` está excluido de Git. Al utilizar otro destino dentro del
repositorio, elegir también una carpeta excluida. Se versionan el manifiesto,
el script y estas instrucciones; los paquetes, SDK y símbolos descargados
permanecen locales.

## Configurar

Ejecutar en PowerShell desde la raíz del repositorio:

```powershell
$cefRoot = (Resolve-Path -LiteralPath "desktop/.deps/cef/cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_windows64").Path
cmake -S desktop -B desktop/build -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143 "-DQt6_DIR=C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6" "-DCEF_ROOT=$cefRoot"
```

La primera línea guarda en la variable PowerShell `$cefRoot` la ruta completa
del SDK preparado en su ubicación predeterminada. Si se utilizó otro destino,
asignar a esa variable la ruta `CEF_ROOT` mostrada por `setup-cef.ps1`.
El argumento `-DCEF_ROOT=$cefRoot` pasa ese valor a la variable `CEF_ROOT` de
CMake, que también se conserva en la caché local.

`-S` selecciona el directorio de fuentes y `-B` el de construcción.
`-G` fija el generador Visual Studio 2022; `-A` selecciona x64 y el Windows
SDK; `-T` selecciona el conjunto de herramientas v143.

`-DQt6_DIR=...` proporciona la carpeta que contiene `Qt6Config.cmake`.
`CMakeLists.txt` declara esta variable sin una ruta predeterminada; el valor
concreto se indica en la terminal y se guarda en el archivo local
`desktop/build/CMakeCache.txt`. No se debe editar ese archivo manualmente.

La configuración exige Qt 6.12.0 exacto y el componente Widgets. Si no se
proporciona `Qt6_DIR` o la carpeta no contiene `Qt6Config.cmake`, CMake termina
con un mensaje que indica cómo proporcionar la ubicación. Si el paquete no
satisface la versión o los componentes requeridos, `find_package` rechaza la
configuración. No se busca automáticamente otro paquete Qt fuera de la
ubicación indicada para resolver `Qt6`.

`CEF_ROOT` debe contener los archivos de construcción y binarios requeridos.
La versión de su cabecera debe coincidir con `cef-sdk.json`. CMake exige un
compilador MSVC que genere Windows x64 y comprueba las cabeceras PE
(Portable Executable) de `bootstrap.exe` y `libcef.dll` de Debug y Release.
Una raíz ausente, un SDK incompleto, una versión distinta o una arquitectura
incompatible detienen la configuración con el diagnóstico correspondiente.

CMake carga `FindCEF.cmake` desde `CEF_ROOT/cmake/`. Ese módulo carga
`cef_variables.cmake` y `cef_macros.cmake`, que proporcionan las rutas y reglas
oficiales. El adaptador se construye desde `CEF_ROOT/libcef_dll/`, con sus
salidas en `desktop/build/libcef_dll_wrapper/`.

Los avisos Qt se localizan a partir de `Qt6_DIR`: la raíz del kit está tres
niveles por encima y contiene `sbom/`. Para la instalación del ejemplo, los
textos comunes están en `C:/Qt/Licenses`. Si se guardaron en otro lugar,
añadir `"-DSBP_QT_LICENSE_DIR=<carpeta-que-contiene-LICENSE>"` al comando de
configuración. CMake diagnostica la ausencia de ese archivo o de cualquiera
de los tres inventarios Qt requeridos.

El aviso `Could NOT find WrapVulkanHeaders` indica que no se han encontrado
las cabeceras de desarrollo de Vulkan. El programa actual no las necesita;
si aparecen `Configuring done` y `Generating done` sin errores, la
configuración ha terminado correctamente.

Si `desktop/build` no existe, CMake lo crea. Si ya existe, debe utilizar
el mismo generador, arquitectura y conjunto de herramientas. Para repetir
la prueba desde cero sin borrar la construcción anterior, se puede sustituir
`desktop/build` por otra carpeta vacía en todos los comandos siguientes.

Las fuentes permanecen fuera del directorio de construcción. La exclusión
`build/` del repositorio mantiene los archivos generados fuera de Git.

## Construir y preparar la carpeta de ejecución

Desde la raíz del repositorio, después de configurar:

```powershell
cmake --build desktop/build --config Debug --target SecureBrowser --parallel 4
cmake --build desktop/build --config Release --target SecureBrowser --parallel 4
```

El flujo de construcción es:

1. Construir `libcef_dll_wrapper` y el ejecutable temporal
   `sbp_toolchain_check`, dependencias del target `SecureBrowser`.
2. Compilar `src/app/app_entry_win.cpp` y enlazar `SecureBrowser.dll` con Qt Widgets,
   el adaptador, el `libcef.lib` correspondiente y las bibliotecas de Windows
   declaradas por CEF.
3. Copiar el bootstrap de esa configuración como `SecureBrowser.exe`, los
   binarios de `CEF_BINARY_FILES` y los recursos de `CEF_RESOURCE_FILES`.
   Ambas listas proceden de `cef_variables.cmake`; `COPY_FILES` y
   `COPY_SINGLE_FILE` proceden de `cef_macros.cmake`.
4. Ejecutar `Qt6::windeployqt`, herramienta del kit seleccionado, con su
   `Qt6::qtpaths`, modo Debug o Release explícito y destino junto a la DLL.
   Se analizan `SecureBrowser.dll` y `sbp_toolchain_check.exe`; la primera
   contiene ahora la ventana principal y el segundo conserva la comprobación
   independiente de Qt.
5. Conservar los avisos CEF y Qt, y aplicar los permisos de lectura y
   ejecución necesarios para el sandbox mediante `SET_LPAC_ACLS`.

Cada configuración tiene su propia salida. Ejemplo de Release:

```text
desktop/build/Release/
├── SecureBrowser.exe
├── SecureBrowser.dll
├── sbp_toolchain_check.exe
├── libcef.dll
├── chrome_elf.dll
├── d3dcompiler_47.dll
├── dxcompiler.dll
├── v8_context_snapshot.bin
├── vk_swiftshader.dll
├── vk_swiftshader_icd.json
├── vulkan-1.dll
├── icudtl.dat
├── chrome_100_percent.pak
├── chrome_200_percent.pak
├── resources.pak
├── locales/
├── Qt6Core.dll
├── Qt6Gui.dll
├── Qt6Widgets.dll
├── platforms/qwindows.dll
├── ...otras dependencias, plugins y traducciones Qt
├── README.txt
├── LICENSE.txt
├── CREDITS.html
└── qt-notices/
    ├── licenses/
    └── sbom/
```

En Debug, las DLL y plugins Qt utilizan sus variantes con sufijo `d`.
`locales/` se conserva completo; los recursos CEF se copian junto al
ejecutable, sin crear un nivel adicional `Resources/`.

`windeployqt` incorpora las dependencias adicionales que detecta, como Qt
Network, Qt SVG, plugins y traducciones. Las opciones
`--no-system-d3d-compiler` y `--no-system-dxc-compiler` conservan las versiones
gráficas copiadas desde CEF. `--no-compiler-runtime` deja el runtime de
Microsoft como requisito del equipo y evita añadir un instalador a la salida.

`CEF_TARGET_OUT_DIR` se fija a `$<TARGET_FILE_DIR:SecureBrowser>` antes de
llamar a `SET_LPAC_ACLS`. Esa macro del SDK ejecuta `icacls` sobre la carpeta
de la configuración y concede lectura y ejecución heredables al identificador
`S-1-15-2-2`, requerido por LPAC (Less Privileged AppContainer). ACL significa
Access Control List (lista de control de acceso).

Estas operaciones se ejecutan después de construir el target. Construir
directamente `sbp_toolchain_check` no prepara la distribución. Si se borran
archivos de una salida ya construida, repetir la preparación con:

```powershell
cmake --build desktop/build --config Debug --target SecureBrowser --clean-first --parallel 4
```

Para Release, sustituir `Debug` por `Release`. Para verificar una construcción
desde cero, configurar y construir en otra carpeta vacía según las
instrucciones anteriores; no es necesario copiar dependencias a mano.

`SecureBrowser.exe` conserva los bytes del `bootstrap.exe` oficial. Nuestro
código está en `SecureBrowser.dll`, que exporta `RunWinMain` con la firma
declarada en `CEF_ROOT/include/cef_sandbox_win.h`.

Para inspeccionar la exportación desde la raíz del repositorio, utilizar
una terminal de desarrollo MSVC donde esté disponible `dumpbin`:

```powershell
dumpbin /exports desktop/build/Debug/SecureBrowser.dll
dumpbin /exports desktop/build/Release/SecureBrowser.dll
```

En ambos resultados debe aparecer el nombre exacto `RunWinMain`.
`SecureBrowser.exe` ejecuta la ventana Qt con CEF inicializado, como se
describe a continuación. El ejecutable temporal conserva su prueba básica.

## Ejecutar la ventana con Qt y CEF

Desde PowerShell, en la raíz del repositorio, ejecutar una configuración cada
vez. El proceso principal abre la ventana y mantiene CEF activo hasta que se
cierra; los subprocesos ejecutan únicamente el recorrido CEF.

```powershell
# Cambiar a "Release" para repetir la misma prueba en esa configuración.
$configuration = "Debug"
cmake --build desktop/build --config $configuration --target SecureBrowser --parallel 4
if ($LASTEXITCODE -ne 0) { throw "La construcción ha fallado; no ejecutar la prueba." }

# Usar rutas completas y guardar el diagnóstico de stderr (salida de errores).
$cefExe = (Resolve-Path -LiteralPath "desktop/build/$configuration/SecureBrowser.exe").Path
$startupLog = Join-Path $env:TEMP "sbp-cef-startup-$configuration.log"
$process = Start-Process -FilePath $cefExe -WorkingDirectory $env:TEMP `
    -RedirectStandardError $startupLog -PassThru
try {
    # Conservar el handle para leer el código aunque el proceso termine rápidamente en PowerShell 5.1.
    $null = $process.Handle
    # Interactuar con la ventana y cerrarla con la X; la terminal espera hasta entonces.
    $process.WaitForExit()
    Get-Content -LiteralPath $startupLog
    $process.ExitCode
} finally {
    $process.Dispose()
}
```

Mantener la ventana abierta durante 30-60 segundos: moverla, cambiar su tamaño,
minimizarla, restaurarla y maximizarla. Debe seguir respondiendo. Después,
cerrarla con la X. Los mensajes del hilo principal deben seguir este orden,
con código final `0`:

```text
Qt application created in the main process.
CEF initialized successfully with bootstrap sandbox context.
CEF browser client prepared; no browser instance created.
Qt event loop starting with CEF initialized.
Qt window destroyed; event loop exit code 0.
CEF shutdown completed.
0
```

Además, debe aparecer una única vez el aviso del contexto:

```text
CEF context initialized; CEF UI thread: 1.
```

Ese mensaje se escribe desde el hilo UI (User Interface, interfaz de usuario)
de CEF, por lo que puede intercalarse con los mensajes del hilo principal.
El valor `1` confirma `CefCurrentlyOn(TID_UI)` tanto en Debug como en Release;
la macro `CEF_REQUIRE_UI_THREAD` añade una comprobación de desarrollo en Debug.

La espera manual no tiene timeout porque la ventana permanece abierta hasta
que se solicita cerrarla. En una comprobación automatizada, limitar el tiempo
de aparición y cierre y terminar solo los procesos creados por esa prueba
si se supera el plazo. Distinguir el plazo de aparición de la ventana del
tiempo de interacción que necesita la prueba.

Se utiliza el objeto devuelto por `Start-Process` para consultar `ExitCode`
después de esperar. `$LASTEXITCODE` se comprueba inmediatamente después de
compilar: ejecutar otro programa sustituiría ese valor y podría ocultar un
fallo de construcción.

Con ninguna otra instancia abierta, comprobar unos segundos después del
cierre que no quedan procesos de la prueba:

```powershell
Get-Process -Name SecureBrowser -ErrorAction SilentlyContinue
```

El comando debe terminar sin listar procesos. Los auxiliares CEF pueden tardar
brevemente en finalizar después del proceso principal; la comprobación local
les concede un máximo de cinco segundos. `sbp_toolchain_check` es otro
ejecutable y su ventana no forma parte de esta comprobación Qt/CEF.

La entrada sigue este flujo:

1. Crear `CefMainArgs` y llamar a `CefExecuteProcess` con el contexto recibido
   del bootstrap. Un resultado mayor o igual que cero termina el subproceso
   devolviendo ese mismo código, antes de crear cualquier objeto Qt.
2. En el principal, exigir un contexto de sandbox no nulo y crear
   `QApplication`. Esta etapa entrega a Qt argumentos mínimos con el nombre
   del programa; sus buffers permanecen vivos hasta destruir la aplicación.
   CEF sigue consultando la línea de comandos nativa. Preparar `CefSettings`
   con sandbox activado, `multi_threaded_message_loop = true` y
   `external_message_pump = false`.
3. Obtener la carpeta local del usuario con `SHGetKnownFolderPath`, construir
   una ruta absoluta propia y copiarla a `settings.root_cache_path`. Liberar
   mediante `CoTaskMemFree` la memoria recibida de Windows.
4. Llamar a `CefInitialize` una vez en el hilo principal con el `BrowserApp`
   conservado por `cef_app`. CEF obtiene su `CefBrowserProcessHandler` y llama
   a `OnContextInitialized` en su hilo UI cuando el contexto está preparado.
   Si la inicialización devuelve `false`,
   devolver `CefGetExitCode` sin llamar a `CefShutdown`. CEF puede indicar un
   error o una salida anticipada normal; se conserva su código.
5. Tras una inicialización correcta, conservar un `BrowserClient` dentro del
   ámbito de la ventana, crear y mostrar el widget en el hilo principal y
   entrar en `qt_app.exec()`. CEF mantiene su propio bucle; no
   se llama a `CefRunMessageLoop`, `CefDoMessageLoopWork` ni se hace espera
   activa para procesar sus eventos.
6. Al cerrar la última ventana, `exec()` devuelve el código Qt. Las llaves
   delimitan el ámbito de `window`: su destructor se ejecuta al salir del
   bloque, antes de `CefShutdown`. Apagar CEF desde el hilo principal y devolver
   el código Qt. `QApplication` se destruye al salir de `RunWinMain`.

Este orden corresponde a la ventana vacía actual. Cuando exista un navegador
incrustado se coordinará su cierre asíncrono, esperando los callbacks
`OnBeforeClose` y liberando sus referencias antes de completar el cierre de
la aplicación. `CefShutdown` apaga el motor global; no sustituye al cierre
de cada instancia `CefBrowser`.

La falta de contexto de sandbox o un fallo al localizar la carpeta del
usuario producen un diagnóstico y código `1`, antes de inicializar el motor.
El diagnóstico propio se escribe en `stderr`. CEF también puede escribir su
log interno en `debug.log`, junto al ejecutable.

Los datos internos de CEF se guardan bajo `%LOCALAPPDATA%\SecureBrowser\CEF`,
normalmente `C:\Users\<usuario>\AppData\Local\SecureBrowser\CEF`. Windows
resuelve esa ubicación para el usuario actual; no se fija un nombre de
usuario, una unidad ni una ruta relativa al directorio de trabajo. CEF crea
los directorios necesarios. Esta raíz evita utilizar la carpeta genérica de
otras aplicaciones CEF; no implementa la persistencia de SBP con SQLite.
Debug y Release utilizan la misma raíz: ejecutar las comprobaciones de forma
secuencial y cerrar la instancia anterior antes de repetirlas.
Si otra instancia sigue activa, CEF puede devolver `24`
(`CEF_RESULT_CODE_NORMAL_EXIT_PROCESS_NOTIFIED` en el SDK fijado): ha avisado
a la instancia existente y solicita terminar esta ejecución. En ese caso se
verá `CEF initialization did not continue: exit code 24.`, sin los mensajes
de inicialización correcta ni de apagado; se conserva ese código normal de
salida anticipada.

En CMake, `NOMINMAX` impide que las macros de Windows interfieran con
`std::min` y `std::max` de las cabeceras CEF. Las opciones de enlace
`/DELAYLOAD:shell32.dll` y `/DELAYLOAD:ole32.dll` retrasan la carga solicitada
por nuestra DLL hasta usar las funciones para localizar la carpeta y liberar
su memoria, después de distinguir el tipo de proceso.

También se difiere la carga de las DLL Qt Core y Qt Widgets mediante
`$<TARGET_FILE_NAME:Qt6::Core>` y `$<TARGET_FILE_NAME:Qt6::Widgets>`, que
seleccionan el nombre Debug o Release del kit. Así no se solicitan esas DLL
desde las importaciones normales de nuestra entrada antes del despacho CEF.
`QT_NO_VERSION_TAGGING` evita la importación de datos de la etiqueta de versión
Qt, incompatible con esta carga diferida en MSVC. La versión exacta del kit
continúa fijada por `find_package(Qt6 6.12.0 EXACT ...)` y las dependencias se
copian desde ese mismo kit.

Para revisar los hilos en Visual Studio, depurar el ejecutable principal de
la configuración elegida y detenerse antes de `qt_app.exec()` y antes de
`CefShutdown()`. Ambos puntos deben alcanzarse desde el hilo principal.
Durante la ejecución, comprobar en la ventana Threads que CEF mantiene su
hilo `CrBrowserMain` separado del hilo propietario de la ventana Qt. Los
widgets se crean y destruyen únicamente en el principal. Todavía no hay
callbacks propios que accedan a widgets desde CEF.

Para observar el reposo, dejar la ventana quieta tras el arranque y consultar
el proceso principal en el Administrador de tareas. Registrar configuración,
duración y condiciones; las actividades internas iniciales de Chromium pueden
generar picos y una muestra local no representa todos los equipos.

La comprobación local en Windows 11 x64, Qt 6.12.0 y CEF
`156.0.14+g4fa4ab5+chromium-156.0.8078.48` registró una ventana vacía, sin
browser, con tres segundos de calentamiento y unos diez segundos de reposo.
Se comprobó su respuesta con un mensaje Windows por segundo, desde otra
carpeta de trabajo y con `PATH` limitado a Windows:

| Configuración | Muestra | CPU del proceso principal respecto a un núcleo |
| --- | --- | --- |
| Debug | 10,103 s | 0,928 % |
| Release | 10,089 s | 0,619 % |

Estos porcentajes incluyen los hilos del proceso principal, no los procesos
auxiliares, y no equivalen al porcentaje global normalizado del Administrador
de tareas. Son una observación de esta muestra, sin comparación de rendimiento
con versiones anteriores. La inspección Win32 del propietario de la ventana y
de las descripciones de los hilos confirmó que Qt y `CrBrowserMain` tenían
identificadores distintos. En ambas configuraciones el cierre devolvió `0`
y no quedaron auxiliares tras el margen de cinco segundos.

## Objetos CEF y propiedad de sus referencias

Las clases propias se definen en [`browser_app.h`](src/browser/browser_app.h) y
[`browser_client.h`](src/browser/browser_client.h), dentro del namespace `sbp`.
`BrowserApp` mantiene sus métodos en el header. `BrowserClient` declara sus
métodos en el header y los implementa en [`browser_client.cpp`](src/browser/browser_client.cpp).
Ninguna de las dos clases crea objetos Qt.

| Objeto | Papeles | Avisos preparados |
| --- | --- | --- |
| `BrowserApp` | `CefApp` y `CefBrowserProcessHandler` | `OnContextInitialized`: contexto global preparado, sin argumentos, en el hilo UI de CEF. |
| `BrowserClient` | `CefClient` y `CefLifeSpanHandler` | `OnAfterCreated`: navegador ya creado; `OnBeforeClose`: fase final del cierre. Reciben un `CefRefPtr<CefBrowser>`. |

`GetBrowserProcessHandler` y `GetLifeSpanHandler` devuelven `this` mediante
`CefRefPtr`: el mismo objeto visto como receptor de avisos, sin construir ni
copiar otro objeto. CEF llama a los callbacks; no son funciones que debamos
invocar manualmente para inicializar o cerrar el navegador.

La propiedad y liberación de referencias es la siguiente:

| Referencia | Quién la conserva | Cuándo se libera |
| --- | --- | --- |
| `cef_app` | Variable local `CefRefPtr<BrowserApp>` en `RunWinMain`. | Al salir de `RunWinMain`, después de `CefShutdown` y antes de destruir `QApplication`. |
| Referencias de CEF al objeto de aplicación y a su handler | CEF durante la vida del contexto. | Durante su limpieza; la referencia local mantiene vivo nuestro objeto mientras dura el arranque y apagado. |
| `browser_client` | Variable local `CefRefPtr<BrowserClient>` dentro del ámbito de la ventana. | Al salir de ese ámbito; su referencia local se libera antes de destruir la ventana principal y antes de `CefShutdown`. |
| Copia de `browser_client` en la solicitud de cierre Qt | Callback guardado por `BrowserWindow`. | Al destruir la ventana y su callback. La referencia local puede haberse liberado antes sin destruir todavía el cliente. |
| Referencia al cliente ligada a la tarea CEF | `base::BindOnce` conserva un `CefRefPtr<BrowserClient>` mientras la tarea está pendiente o ejecutándose. | Al terminar o destruirse la tarea, sin dejar un puntero al cliente sin propietario. |
| Parámetro `browser` de los callbacks | Referencia administrada durante cada llamada. | Al terminar la llamada. El cliente no la almacena en ningún miembro. |

`IMPLEMENT_REFCOUNTING` proporciona `AddRef`, `Release` y el contador que
utiliza `CefRefPtr`. Los destructores privados impiden un `delete` externo;
la última referencia destruye el objeto. Las operaciones de copia del objeto
están prohibidas, pero se pueden copiar sus referencias administradas.
`.get()` entrega el puntero conservando nuestra referencia; al recibirlo como
`CefRefPtr`, la API de CEF gestiona las referencias que necesite mantener.

Ninguna de las clases conserva una referencia administrada a sí misma, a la
otra clase ni a un navegador. Los callbacks de `BrowserClient` capturan la
ventana Qt por referencia para encolar avisos, sin adquirir su propiedad.
La ventana se mantiene viva hasta la confirmación del cierre; estas capturas
no crean un ciclo de propiedad.
`OnContextInitialized`, `OnAfterCreated` y `OnBeforeClose` no modifican widgets
desde el hilo CEF. `BrowserApp` conserva una función proporcionada al construirlo;
tras el diagnóstico de contexto preparado, la ejecuta en el hilo CEF. Esa función
utiliza `QMetaObject::invokeMethod` con `Qt::QueuedConnection` para enviar el aviso
al hilo de `QApplication`, sin bloquear el hilo CEF. Las variables capturadas
permanecen vivas hasta después de `CefShutdown` y de liberar nuestra referencia
a `BrowserApp`.

El receptor escribe `CEF context notification received; Qt main
thread: 1.`. Debe aparecer una vez al procesar el bucle Qt; `1` confirma el hilo
principal tanto en Debug como en Release. El mensaje de contexto CEF original
sigue procediendo de su propio hilo UI.

En el hilo Qt se coordinan dos condiciones: aviso de contexto recibido y
`HWND` (identificador de ventana de Windows) del widget anfitrión disponible.
Cuando ambas se cumplen, se prepara una vez `CefWindowInfo`: `SetAsChild`
configura la ventana hija, `GetClientRect` obtiene su área nativa en píxeles
y `CEF_RUNTIME_STYLE_ALLOY` selecciona el estilo acordado. Se mantiene
`windowless_rendering_enabled = false`. El diagnóstico muestra el padre,
el tamaño y el estilo (`2` identifica Alloy en este SDK).

Esta preparación todavía no llama a `CefBrowserHost::CreateBrowser`. El tamaño
es una captura inicial; el ajuste del contenido CEF al redimensionar se
incorporará con la instancia real. Al salir del bucle Qt se retiran el `HWND`
y los parámetros antes de destruir los widgets; una notificación posterior
ya no encontrará un anfitrión disponible.

La ventana principal utiliza [`BrowserWindow`](src/ui/browser_window.h), una subclase
de `QWidget`. El header declara la clase y sus métodos; su implementación está
en [`browser_window.cpp`](src/ui/browser_window.cpp), que se compila como parte del
target `SecureBrowser`. Su `closeEvent` rechaza el cierre inmediato y comunica una única
solicitud mediante el callback registrado con `SetCloseRequestHandler`.
`ConfirmClose` permite después aceptar el cierre, conservando los widgets
hasta ese momento. Todos estos pasos se ejecutan en el hilo Qt y no muestran
un diálogo de confirmación al usuario.

La solicitud sale del hilo Qt mediante `CefPostTask(TID_UI, ...)`.
`base::BindOnce(&BrowserClient::RequestClose, browser_client)` prepara una
llamada al método del cliente y conserva una referencia administrada a ese
objeto hasta ejecutar o destruir la tarea. Como todavía no existe un navegador,
`RequestClose` cubre únicamente ese caso y devuelve la confirmación hacia la
cola de la ventana Qt mediante la función registrada en el cliente.
El diagnóstico debe seguir este orden:

```text
Qt close requested; native host remains alive.
CEF client close request received; CEF UI thread: 1.
Qt close confirmed; native host valid: 1.
Qt close accepted after confirmation.
Qt window destroyed; event loop exit code 0.
CEF shutdown completed.
```

`BrowserClient` ya recibe dos funciones al construirse. `OnAfterCreated`
entrega únicamente el identificador del navegador a la primera, que encola
su diagnóstico en Qt. `OnBeforeClose` ejecuta la segunda, que encola
`ConfirmClose` en la ventana principal. El cliente no guarda un `CefBrowser`
ni transfiere esa referencia a las funciones encoladas.

Esta comprobación sigue verificando el cierre de una ventana sin navegador:
recorre Qt → hilo UI de CEF → Qt, manteniendo el anfitrión vivo hasta confirmar.
Todavía no solicita `CloseBrowser` ni ejecuta los callbacks CEF de creación o
cierre. Antes de incorporar una instancia, se ampliará `RequestClose` para
solicitar su cierre y esperar `OnBeforeClose`, en lugar de confirmar
directamente. La ventana Qt y CEF permanecen activos hasta esa confirmación.

En esta etapa se verifica que el aviso de contexto llega una vez en el hilo
esperado, que se prepara el cliente y que la ventana abre y cierra con salida
`0`. Los avisos `OnAfterCreated` y `OnBeforeClose` todavía no se ejecutan:
se validarán con una instancia real en #88. No se simulan sus llamadas desde
el hilo Qt. La creación y el cierre coordinado del navegador seguirán
respetando las responsabilidades descritas en el ADR-0011.

## Ventana Qt independiente de comprobación del kit

Construir el target `SecureBrowser` para preparar también las dependencias.
Después, ejecutar la ventana temporal sin añadir Qt al `PATH` (variable de
entorno de búsqueda de ejecutables). Desde la raíz del repositorio:

```powershell
cmake --build desktop/build --config Debug --target SecureBrowser --parallel 4
if ($LASTEXITCODE -ne 0) { throw "La construcción Debug ha fallado." }
.\desktop\build\Debug\sbp_toolchain_check.exe
$LASTEXITCODE
```

Desde la raíz del repositorio, repetir para Release en la misma terminal:

```powershell
cmake --build desktop/build --config Release --target SecureBrowser --parallel 4
if ($LASTEXITCODE -ne 0) { throw "La construcción Release ha fallado." }
.\desktop\build\Release\sbp_toolchain_check.exe
$LASTEXITCODE
```

Para comprobar que no depende de la carpeta de trabajo ni de Qt en el `PATH`,
utilizar una terminal PowerShell de prueba. Guardar primero la ruta completa
y ejecutar desde la carpeta temporal, restaurando el entorno al terminar:

```powershell
$windowExe = (Resolve-Path -LiteralPath "desktop/build/Release/sbp_toolchain_check.exe").Path
$previousPath = $env:PATH
Push-Location $env:TEMP
try {
    $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
    & $windowExe
    $LASTEXITCODE
} finally {
    $env:PATH = $previousPath
    Pop-Location
}
```

Repetir con `Debug` en la ruta. La terminal de prueba no debe tener variables
Qt personalizadas que redirijan los plugins, como `QT_PLUGIN_PATH` o
`QT_QPA_PLATFORM_PLUGIN_PATH`.

Comprobar que cada comando de CMake termina sin errores antes de continuar.
El código de salida inmediatamente después de ejecutar cada programa debe
ser `0`. La ejecución permanece activa hasta cerrar la ventana; no imprime
texto en la terminal.

En ambas configuraciones, comprobar manualmente:

- La ventana aparece con el título **SecureBrowser** y el contenido vacío.
- Se puede redimensionar y sigue respondiendo.
- Se puede minimizar, restaurar, maximizar y volver al tamaño anterior.
- Al cerrarla, desaparece la ventana, termina el proceso y la terminal
  vuelve a estar disponible con código de salida `0`.

`--config` selecciona la configuración y `--target` el ejecutable que se
construye. El generador Visual Studio admite ambas configuraciones dentro
de una misma carpeta de construcción, con salidas separadas en `Debug/`
y `Release/`; no se utiliza `CMAKE_BUILD_TYPE` para elegir entre ellas.

Los targets propios y el adaptador requieren C++20 sin extensiones del
compilador. Utilizan la CRT
(C Run-Time Library, biblioteca de ejecución de C) dinámica: `/MDd` en Debug
y `/MD` en Release. Los targets propios la seleccionan mediante
`CMAKE_MSVC_RUNTIME_LIBRARY`; las reglas del adaptador reciben
`CEF_RUNTIME_LIBRARY_FLAG=/MD` antes de cargar CEF.

## Dependencias de desarrollo y ejecución

| Elementos del kit Qt | Uso |
| --- | --- |
| `include/`, `lib/cmake/` y bibliotecas de enlace `.lib` en `lib/` | Configurar y compilar en el equipo de desarrollo. |
| `bin/windeployqt.exe` | Preparar las dependencias Qt de una salida de ejecución. Se utiliza desde el mismo kit. |
| `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll` | Bibliotecas Release necesarias cuando la aplicación utiliza esos módulos. Las variantes Debug llevan sufijo `d`. |
| `plugins/platforms/qwindows.dll` y su variante Debug `qwindowsd.dll` | Plugin de plataforma para la ventana Qt, copiado a `platforms/` dentro de la salida. |

| Elementos de CEF | Uso |
| --- | --- |
| Cabeceras, módulos CMake y fuentes de `libcef_dll/` | Configurar y compilar con el SDK. |
| `libcef.lib` y `libcef_dll_wrapper.lib` | Bibliotecas de enlace utilizadas al construir nuestra DLL. |
| `bootstrap.exe` | Se copia como `SecureBrowser.exe` junto a nuestra DLL. |
| `libcef.dll`, otros binarios CEF y `Resources/` | Dependencias de ejecución copiadas automáticamente junto al ejecutable. |

La preparación sigue el ADR-0011 y utiliza la herramienta de la
[guía de distribución en Windows](https://doc.qt.io/qt-6/windows-deployment.html).
El SDK de desarrollo completo no forma parte de la carpeta de ejecución.

Se han comprobado la apertura y el cierre normal de la ventana en Debug y
Release desde la carpeta temporal, con `PATH` limitado a Windows y código
de salida `0`. Las bibliotecas Qt y el plugin de plataforma se cargan desde
la salida preparada. Esto verifica la ventana Qt en el equipo de desarrollo;
la ventana principal con CEF activo se verifica mediante `SecureBrowser.exe`,
incluyendo el orden de cierre y el fin de los procesos auxiliares. El navegador
CEF incrustado y su coordinación de cierre se incorporarán en tareas posteriores.

La instalación Qt queda fuera del repositorio; `build/`, `CMakeCache.txt`
y `CMakeUserPresets.json` están excluidos por `.gitignore`. Solo se versionan
las fuentes, la configuración del proyecto y las instrucciones.

## Licencias y avisos de terceros

Las condiciones de Qt dependen de los módulos y de la opción de licencia
utilizada. Las referencias son la
[documentación de licencias Qt](https://doc.qt.io/qt-6/licensing.html) y el
[inventario de código de terceros](https://doc.qt.io/qt-6/licenses-used-in-qt.html).
La salida conserva los siguientes archivos sin modificar sus textos:

| Destino | Origen y contenido |
| --- | --- |
| `README.txt`, `LICENSE.txt`, `CREDITS.html` | Instrucciones, licencia y créditos de la distribución CEF fijada. |
| `qt-notices/licenses/` | Textos comunes del instalador, desde `SBP_QT_LICENSE_DIR`; incluye `LICENSE` con los textos GNU GPL (General Public License) y LGPL (Lesser General Public License). |
| `qt-notices/sbom/qtbase-6.12.0.spdx.json` | Inventario del kit para Qt Core, Gui, Widgets, Network y plugins asociados. |
| `qt-notices/sbom/qtsvg-6.12.0.spdx.json` | Inventario del kit para Qt SVG y sus plugins. |
| `qt-notices/sbom/qttranslations-6.12.0.spdx.json` | Inventario del kit para las traducciones. |

SBOM significa Software Bill of Materials (inventario de componentes de
software); SPDX significa Software Package Data Exchange. Los documentos
conservan versiones, copyright, identificadores de licencia y textos de
licencias específicas proporcionados por Qt. Se copian completos por módulo
y también describen archivos de desarrollo o componentes que no se despliegan.
Si se incorporan otros módulos en el futuro, revisar sus inventarios y avisos.

Los avisos se copian desde las instalaciones locales seleccionadas; la
configuración y construcción no descargan textos. Esta etapa prepara una
carpeta de ejecución local; el instalador y la publicación siguen fuera del
alcance de Foundation.
