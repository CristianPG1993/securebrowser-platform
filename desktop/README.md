# SecureBrowser Desktop

Desktop Foundation es el navegador local de SBP: Qt 6 Widgets contiene una
ventana hija CEF con carga HTTP/HTTPS, barra de direcciones, Atrás, Adelante,
Recargar, gestión de foco y tamaño y cierre coordinado. La barra refleja la
URL notificada por CEF; un dominio sin esquema se interpreta como HTTPS.
Las páginas que solicitan `beforeunload` pueden preguntar antes de salir.

Esta guía corresponde a [#95](https://github.com/CristianPG1993/securebrowser-platform/issues/95).
Describe cómo preparar un clon, compilar Debug y Release y abrir
`SecureBrowser.exe` en Windows 11 x64. El backend MVP permanece congelado:
Foundation todavía no se comunica con él ni implementa sus políticas.

Seguir los apartados de preparación en orden y detenerse si un comando falla.
Los scripts están bajo `ci/` porque también los utiliza Actions; se pueden
invocar localmente con los argumentos indicados.

- [Requisitos](#requisitos) y [clon limpio](#obtener-un-clon-limpio).
- [Herramientas](#preparar-las-herramientas), [Qt](#preparar-qt) y [CEF](#preparar-cef).
- [Configuración](#configurar), [construcción](#construir-y-preparar-la-carpeta-de-ejecución) y [ejecución](#ejecutar-la-ventana-con-qt-y-cef).
- [Verificaciones](#verificaciones-automáticas-y-manuales), [diagnóstico](#diagnóstico-local-de-arranque-y-carga) y [problemas conocidos](#problemas-conocidos).
- [Registro de validación desde un clon limpio](docs/clean-clone-validation.md).
- [Notas de implementación y comprobaciones anteriores](docs/implementation-notes.md).

## Requisitos

Para reproducir la construcción, utilizar el entorno acordado en la siguiente
tabla. Las versiones de herramientas están recogidas en
[`ci/toolchain.json`](ci/toolchain.json); CEF conserva su selección en
[`cef-sdk.json`](cef-sdk.json).

| Elemento | Versión o selección |
| --- | --- |
| Sistema de desarrollo y validación visual | Windows 11 x64. |
| Compilador | Visual Studio Build Tools 2022 con las herramientas C++ MSVC (Microsoft Visual C++) v143 para x64. |
| Lenguaje | C++20. |
| Windows SDK (Software Development Kit, kit de desarrollo de software) | Paquete `10.0.28000.2270`; identificador de instalación para CMake: `10.0.28000.0`. |
| CMake de referencia | `3.29.2`. |
| Extractor de la ruta principal | 7-Zip `26.05` x64, instalado en `%ProgramFiles%\7-Zip`. |
| Generador y arquitectura | `Visual Studio 17 2022`, `x64`, toolset `v143`. |
| Qt | `6.12.0`, kit `msvc2022_64`, con Qt Widgets y bibliotecas Debug y Release. |
| CEF | `156.0.14+g4fa4ab5+chromium-156.0.8078.48`, distribución Standard `windows64`. |

El proyecto CMake admite versiones desde 3.29; los scripts de configuración
de CI exigen `3.29.2` para reproducir la referencia fijada. El generador de
Visual Studio permite construir Debug y Release desde la misma configuración.
El SDK CEF ya contiene el motor precompilado; se compilan nuestro código y
el adaptador oficial `libcef_dll_wrapper`.

Para obtener el repositorio y preparar las dependencias se necesitan Git,
Internet y Windows PowerShell 5.1 o PowerShell 7. La descarga automatizada de
Qt necesita Python 3.10 o posterior, con `venv` y `pip`, disponible como
`python`. Ese mínimo procede de la [revisión fijada de aqtinstall](https://github.com/miurahr/aqtinstall/blob/7e5a5c3d95cf962cfc2f36c86ffa0d2c07f1a0d4/pyproject.toml).
Git y Python son herramientas de preparación; sus versiones no están fijadas
en el manifiesto de CI. Anotar las utilizadas al registrar la validación.
La ruta principal de esta guía utiliza 7-Zip como CI; el script compartido
admite también el `tar.exe` incluido en Windows.

La preparación del runtime necesita los inventarios `sbom/` del kit Qt y
sus avisos comunes. Estos últimos pueden proceder de `Licenses/` del
instalador o de los [avisos conservados para Qt 6.12.0](third_party/qt/6.12.0/README.md)
en el repositorio, seleccionados con `SBP_QT_LICENSE_DIR`.

Release necesita un runtime de Microsoft Visual C++ v143 x64 compatible.
Debug utiliza las bibliotecas de depuración del entorno de desarrollo.

Las decisiones de integración están recogidas en el
[ADR-0011 (Architecture Decision Record, registro de decisión arquitectónica)](../docs/adr/0011-desktop-cef-integration.md).

## Obtener un clon limpio

Abrir PowerShell en la carpeta donde se quiera guardar la nueva copia, fuera
de otro clon del proyecto. El directorio `securebrowser-platform-clean` debe
estar disponible; si ya existe, elegir otro nombre en ambos comandos.

```powershell
git clone --branch main https://github.com/CristianPG1993/securebrowser-platform.git securebrowser-platform-clean
if ($LASTEXITCODE -ne 0) { throw 'No se ha podido crear el clon limpio.' }
Set-Location -LiteralPath .\securebrowser-platform-clean -ErrorAction Stop
git rev-parse HEAD
git status --short
```

`git rev-parse HEAD` muestra el SHA del commit que se va a comprobar; guardarlo
con los resultados de la validación. `git status --short` debe terminar sin
mostrar cambios en esta copia recién creada. Para validar una PR antes del
merge, sustituir `main` por su rama publicada en el comando de clonación.

Los comandos de preparación, configuración, compilación y ejecución de esta
guía se ejecutan desde esta raíz del repositorio, en la misma terminal
PowerShell. Las dependencias de `desktop/.deps/` y las salidas de
`desktop/build/` se preparan en los pasos siguientes. Para acreditar una
construcción limpia, mantener esas carpetas independientes de las de otros
clones.

## Preparar las herramientas

Instalar [Git para Windows](https://git-scm.com/download/win) y
[Python para Windows](https://www.python.org/downloads/windows/) 3.10 o
posterior, con `venv` y `pip`. Comprobar que la terminal encuentra las
herramientas antes de descargar Qt:

```powershell
git --version
python --version
```

Instalar Visual Studio Build Tools **2022** con las herramientas C++ de
escritorio MSVC v143 para x64. Seguir la [guía de Microsoft](https://learn.microsoft.com/en-us/cpp/build/vscpp-step-0-installation?view=msvc-170)
seleccionando esa versión y ese toolset. El editor, incluido VS Code, se puede
elegir aparte; el proyecto utiliza el compilador MSVC y el generador de la tabla.

El [Windows SDK fijado](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads)
es el paquete `10.0.28000.2270`. Si hay que instalarlo, abrir una terminal
con permisos de administración y situarse en la raíz del clon. Si ya está
instalado, se puede comprobar desde la terminal habitual:

```powershell
.\desktop\ci\setup-windows-sdk.ps1
if ($LASTEXITCODE -ne 0) { throw 'La preparación del Windows SDK ha fallado.' }
```

Si necesita instalarlo, el script verifica SHA-256 y firma Microsoft antes de
lanzar el instalador. Después de esa instalación, continuar el resto de la
guía en la terminal habitual. Una instalación existente que supera las comprobaciones
se reutiliza. `-CheckOnly` permite comprobarla sin instalar nada.
El paquete y la carpeta `10.0.28000.0` son identificadores distintos.

Instalar [7-Zip 26.05 x64 desde su origen oficial](https://www.7-zip.org/),
en su ubicación habitual `%ProgramFiles%\7-Zip`. El script de CEF comprueba
esa ubicación y la versión del ejecutable.

Preparar CMake en la terminal donde se continuará trabajando:

```powershell
.\desktop\ci\setup-cmake.ps1
if ($LASTEXITCODE -ne 0) { throw 'La preparación de CMake ha fallado.' }
cmake --version
```

El resultado debe empezar por `cmake version 3.29.2`. El script selecciona una
instalación válida o descarga el ZIP oficial y comprueba su SHA-256. Añade su
carpeta al `PATH` de esta terminal, sin modificar el `PATH` del sistema.
Si se abre otra terminal, repetir este paso y recuperar las variables de Qt y
CEF antes de configurar.

## Preparar Qt

La preparación automatizada utiliza [`ci/setup-qt.ps1`](ci/setup-qt.ps1),
el mismo script que CI. Necesita Git y un intérprete Python 3.10 o posterior disponible
como `python` en el `PATH` (variable de entorno de búsqueda de ejecutables).
Si el intérprete tiene otro nombre o ubicación, se puede proporcionar con
`-PythonExecutable`.

Desde la raíz del clon limpio:

```powershell
.\desktop\ci\setup-qt.ps1
if ($LASTEXITCODE -ne 0) { throw 'La preparación de Qt ha fallado.' }
$qtKitDirectory = (Resolve-Path -LiteralPath 'desktop/.deps/qt/6.12.0/msvc2022_64').Path
```

El script lee la versión, la arquitectura, los paquetes y la revisión completa
de aqtinstall desde [`ci/toolchain.json`](ci/toolchain.json). Prepara aqtinstall
en un entorno Python aislado dentro de `desktop/.deps/tools/` y descarga el
kit Qt en `desktop/.deps/qt/6.12.0/msvc2022_64/`. aqtinstall comprueba los hashes
de los paquetes descargados. Ambas carpetas permanecen fuera de Git.

Antes de terminar, comprueba la versión `6.12.0`, la arquitectura MSVC x64,
las bibliotecas y plugins de Debug y Release y los inventarios `sbom/`.
El mensaje esperado comienza por `Kit Qt 6.12.0 MSVC x64 preparado:` y el
código de salida debe ser 0. La variable `$qtKitDirectory` guarda la ruta
completa del kit para utilizarla al configurar CMake.

Al repetir el comando con un kit ya preparado, verifica los archivos
existentes. Si el kit está incompleto o es incompatible, termina con un
diagnóstico; corregir esa instalación antes de continuar.

### Utilizar un kit Qt existente

Como alternativa a la descarga anterior, se puede comprobar una instalación
Qt `6.12.0`, kit `msvc2022_64`, con Debug y Release. Para una instalación bajo
`C:/Qt`, ejecutar este bloque en lugar del anterior:

```powershell
.\desktop\ci\setup-qt.ps1 -QtDirectory 'C:/Qt' -CheckOnly
if ($LASTEXITCODE -ne 0) { throw 'La comprobación del kit Qt ha fallado.' }
$qtKitDirectory = (Resolve-Path -LiteralPath 'C:/Qt/6.12.0/msvc2022_64').Path
```

`-QtDirectory` recibe la raíz que contiene las versiones de Qt, por ejemplo
`C:/Qt`; `$qtKitDirectory` apunta al kit concreto dentro de esa raíz. Sustituir
ambas rutas si la instalación está en otro lugar. `-CheckOnly` valida el kit
sin descargar paquetes ni necesitar Python. Las dos opciones seleccionan
explícitamente el mismo kit y dejan preparada `$qtKitDirectory`.

## Preparar CEF

El [manifiesto CEF](cef-sdk.json) fija la distribución Standard `windows64`,
su versión, URL oficial, tamaño y SHA-1. El motor ya está compilado en ese SDK.
Desde la raíz del clon, utilizar la misma extracción con 7-Zip que CI:

```powershell
.\desktop\ci\setup-cef.ps1
if ($LASTEXITCODE -ne 0) { throw 'La preparación de CEF ha fallado.' }
$cefManifest = Get-Content -LiteralPath 'desktop/cef-sdk.json' -Raw -Encoding UTF8 | ConvertFrom-Json
$cefDirectoryName = $cefManifest.archive -replace '\.tar\.bz2$', ''
$cefRoot = (Resolve-Path -LiteralPath (Join-Path 'desktop/.deps/cef' $cefDirectoryName)).Path
```

La primera descarga ocupa aproximadamente 367 MB. El script compartido
[`scripts/setup-cef.ps1`](../scripts/setup-cef.ps1) comprueba tamaño y SHA-1,
descomprime BZip2 y extrae el TAR en una carpeta temporal. Valida archivos,
versión y arquitectura x64 de Debug y Release antes de mover el SDK a su destino.

| Ruta | Contenido |
| --- | --- |
| `desktop/.deps/downloads/` | Archivo comprimido verificado. |
| `desktop/.deps/cef/<directorio-del-paquete>/` | Cabeceras, CMake, adaptador, binarios, recursos y avisos del SDK. |

`$cefRoot` debe apuntar al directorio que contiene `include/`, `cmake/`,
`Debug/`, `Release/` y `Resources/`. La salida del script muestra también
`CEF_ROOT: <ruta-completa>`. CMake recibe esa ruta explícitamente; configurar
el proyecto no descarga CEF.

Al repetir la preparación, un SDK existente válido se reutiliza sin descargar
ni extraer. Se comprueban sus archivos esenciales, versión y arquitectura;
no se recalcula un hash de todos los archivos extraídos. Una descarga o SDK
existente incorrecto se conserva para revisión y produce un error.

Como alternativa local, el script compartido admite `tar.exe` por defecto y
`-DependenciesDirectory` para elegir otro destino. Un ejemplo fuera del clon:

```powershell
.\scripts\setup-cef.ps1 -DependenciesDirectory 'C:/SBP-deps'
if ($LASTEXITCODE -ne 0) { throw 'La preparación alternativa de CEF ha fallado.' }
```

En ese caso, asignar a `$cefRoot` la ruta `CEF_ROOT` mostrada antes de configurar.
Para validar una descarga limpia, utilizar el destino nuevo del clon.
`desktop/.deps/` está excluido de Git.

## Configurar

Después de preparar las herramientas, Qt y CEF en la misma terminal:

```powershell
.\desktop\ci\configure.ps1 -QtKitDirectory $qtKitDirectory -CefRoot $cefRoot
if ($LASTEXITCODE -ne 0) { throw 'La configuración de CMake ha fallado.' }
```

El script fija el generador Visual Studio 2022, x64, v143, Windows SDK
`10.0.28000.0` y las configuraciones Debug/Release en `desktop/build/`.
Selecciona los [avisos conservados de Qt 6.12.0](third_party/qt/6.12.0/README.md).
Comprueba que CMake sea `3.29.2`; CMake comprueba el kit Qt exacto y la versión
y arquitectura del SDK CEF.

Los argumentos que recibe CMake se pueden expresar directamente así.
Este bloque es una alternativa al anterior:

```powershell
$qt6Directory = Join-Path $qtKitDirectory 'lib/cmake/Qt6'
$qtLicenseDirectory = (Resolve-Path -LiteralPath 'desktop/third_party/qt/6.12.0/licenses').Path
cmake -S desktop -B desktop/build -G 'Visual Studio 17 2022' -A 'x64,version=10.0.28000.0' -T v143 '-DCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release' "-DQt6_DIR:PATH=$qt6Directory" "-DCEF_ROOT:PATH=$cefRoot" "-DSBP_QT_LICENSE_DIR:PATH=$qtLicenseDirectory"
if ($LASTEXITCODE -ne 0) { throw 'La configuración directa de CMake ha fallado.' }
```

`Qt6_DIR` contiene `Qt6Config.cmake`, `CEF_ROOT` contiene el SDK y
`SBP_QT_LICENSE_DIR` contiene el archivo `LICENSE` y los demás avisos comunes.
Los inventarios `sbom/` se obtienen del kit Qt seleccionado.
`-S` indica fuentes y `-B` las salidas. Las rutas concretas se guardan en
`CMakeCache.txt` local; no editarlo ni versionarlo.

El generador es multiconfiguración: `--config` elegirá Debug o Release al
construir. No se utiliza `CMAKE_BUILD_TYPE` para elegir entre ellas.
Un directorio de construcción conserva su generador y toolset. Para otra
configuración de herramientas, usar un directorio nuevo. Los comprobadores de
esta guía esperan las salidas predeterminadas en `desktop/build/`.

## Construir y preparar la carpeta de ejecución

Desde la raíz del repositorio:

```powershell
foreach ($configuration in @('Debug', 'Release')) {
    cmake --build desktop/build --config $configuration --target SecureBrowser --parallel 4
    if ($LASTEXITCODE -ne 0) { throw "La construcción $configuration ha fallado." }
}
```

`--parallel 4` permite hasta cuatro trabajos de compilación simultáneos.
CMake construye nuestra DLL, el adaptador `libcef_dll_wrapper` y la ventana
independiente `sbp_toolchain_check`. El post-build prepara el runtime completo
de cada configuración:

1. Copia el bootstrap y los binarios y recursos de esa configuración de CEF.
2. Ejecuta `windeployqt` del kit seleccionado para copiar las dependencias Qt.
3. Conserva avisos CEF y Qt y aplica los permisos de lectura y ejecución LPAC.

Cada configuración termina en `desktop/build/Debug/` o `desktop/build/Release/`.
Ejemplo abreviado de Release:

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

En Debug, las DLL y plugins Qt llevan sus nombres de depuración, como
`Qt6Cored.dll` y `qwindowsd.dll`. Los recursos de CEF quedan junto al
ejecutable; `locales/` se copia completo.

### Qué se construye y qué se redistribuye

| Elemento | Procedencia y función |
| --- | --- |
| `SecureBrowser.exe` | Bootstrap oficial de CEF renombrado, correspondiente a Debug o Release. |
| `SecureBrowser.dll` | Código propio compilado; exporta `RunWinMain`, que invoca el bootstrap. |
| `libcef.dll` y otros binarios CEF | Motor y auxiliares precompilados del SDK fijado. |
| `libcef_dll_wrapper` | Adaptador oficial compilado y enlazado con nuestra DLL. |
| DLL, plugins y traducciones Qt | Copiados desde el kit seleccionado mediante `windeployqt`. |
| `sbp_toolchain_check.exe` | Comprobación independiente de una ventana Qt; no es el navegador. |
| `runtime-inventory-Debug.json` y `runtime-inventory-Release.json` | Inventarios generados en `desktop/build/` para comprobar las salidas. |

Nuestra DLL y el adaptador usan CRT dinámica: `/MDd` en Debug y `/MD` en Release.
La [preparación Qt](https://doc.qt.io/qt-6/windows-deployment.html) usa
`--no-compiler-runtime`: el runtime MSVC sigue siendo requisito del equipo.
No se incorporan instaladores de Microsoft a la salida. Las bibliotecas
gráficas que entrega CEF se conservan mediante las opciones de despliegue Qt.

El SDK completo y sus símbolos son dependencias de desarrollo, no parte del
runtime. Para ejecutar se necesita la carpeta de salida preparada, con sus
recursos y plugins.

Si se ha retirado un archivo del runtime después de construir, forzar también
el post-build:

```powershell
cmake --build desktop/build --config Debug --target SecureBrowser --clean-first --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'La reconstrucción Debug ha fallado.' }
```

Repetir con Release si corresponde. Construir solo `sbp_toolchain_check` no
prepara el despliegue completo de SecureBrowser.

## Ejecutar la ventana con Qt y CEF

Ejecutar una configuración cada vez y cerrar las instancias anteriores.
Debug y Release comparten `%LOCALAPPDATA%\SecureBrowser\CEF`.

Desde la raíz del repositorio, después de compilar:

```powershell
$configuration = 'Debug' # Repetir este bloque con 'Release'.
$cefExe = (Resolve-Path -LiteralPath "desktop/build/$configuration/SecureBrowser.exe").Path
$process = Start-Process -FilePath $cefExe -WorkingDirectory $env:TEMP -PassThru
try {
    $null = $process.Handle
    $testProcessId = $process.Id
    Write-Host "SecureBrowser $configuration; PID: $testProcessId. Cerrar la ventana para continuar."
    $process.WaitForExit()
    $applicationExitCode = $process.ExitCode
    Write-Host "Código de salida: $applicationExitCode"
    if ($applicationExitCode -ne 0) { throw 'La aplicación no ha terminado con código 0.' }
} finally {
    $process.Dispose()
}
```

La terminal espera mientras se interactúa con la ventana. Debe mostrarse
`https://example.com/` dentro de Qt. La barra permite introducir otra URL o
un dominio, pulsar Enter y utilizar Atrás, Adelante y Recargar.
Cerrar con la X; el resultado normal es `0`. También se puede abrir el EXE
desde el Explorador; los diagnósticos se guardan igualmente.

Con ninguna otra instancia abierta, comprobar que no quedan procesos cinco
segundos después del cierre:

```powershell
Start-Sleep -Seconds 5
Get-Process -Name SecureBrowser -ErrorAction SilentlyContinue
```

El segundo comando debe terminar sin listar procesos.
`sbp_toolchain_check.exe` muestra una ventana Qt independiente; no acredita
que CEF haya creado o renderizado el navegador.

### Bootstrap, sandbox y cierre

El mismo EXE inicia el principal y los subprocesos de CEF. El bootstrap
proporciona el contexto de sandbox y carga `SecureBrowser.dll`. La entrada
despacha los subprocesos antes de crear Qt; en el principal crea
`QApplication`, inicializa CEF y ejecuta la sesión.

El sandbox se mantiene activado. La construcción utiliza `SET_LPAC_ACLS`
para conceder lectura y ejecución heredables al identificador `S-1-15-2-2`
de LPAC (Less Privileged AppContainer) sobre la carpeta de runtime.
Al trasladar una salida a otro directorio, comprobar sus permisos; la guía
valida la carpeta preparada por CMake.

Qt procesa sus eventos en el hilo principal y CEF tiene su propio hilo UI.
El cierre conserva el anfitrión Qt hasta la confirmación posterior a
`OnBeforeClose`; después destruye los widgets y llama a `CefShutdown`.
Los detalles de callbacks y referencias están en las
[notas de implementación](docs/implementation-notes.md).

## Verificaciones automáticas y manuales

La [guía de pruebas](tests/README.md) contiene la batería y su cobertura.
Con ambas configuraciones construidas y sin instancias abiertas:

```powershell
.\desktop\tests\check-configuration-rejection.ps1 -TimeoutSeconds 120
if ($LASTEXITCODE -ne 0) { throw 'Ha fallado la comprobación de configuración.' }

foreach ($configuration in @('Debug', 'Release')) {
    .\desktop\tests\check-runtime.ps1 -InventoryPath "desktop/build/runtime-inventory-$configuration.json"
    if ($LASTEXITCODE -ne 0) { throw "Runtime $configuration incompleto." }

    .\desktop\tests\check-startup.ps1 -Configuration $configuration -StartupTimeoutSeconds 60 -CloseTimeoutSeconds 60
    if ($LASTEXITCODE -ne 0) { throw "Ha fallado el arranque o cierre $configuration." }
}
```

Los comprobadores deben devolver `0`. En el caso de Qt ausente, CMake debe
fallar y el comprobador devuelve `0` al reconocer ese rechazo esperado.
El inventario comprueba presencia de archivos. La prueba de arranque detecta
la ventana hija nativa CEF y verifica cierre y logs; no comprueba la carga
de red ni el contenido renderizado.

Repetir manualmente en Debug y Release la [matriz visual](tests/README.md#matriz-visual-manual):
carga HTTPS, navegación y barra de direcciones, foco y Alt+Tab, cambio de tamaño,
minimizar/restaurar y cierre sin procesos restantes. Registrar sus resultados
para la revisión y el equipo utilizados.

### Confirmación de abandono y pruebas manuales de cierre

La confirmación requiere que la propia página registre `beforeunload`.
**Salir** permite continuar; **Quedarme**, Escape o la X del diálogo cancelan.
SecureBrowser no detecta por sí mismo cualquier campo editado o sin guardar.

Los auxiliares están en `tests/manual/` y no forman parte del despliegue.
Desde una segunda terminal en la raíz:

```powershell
python -m http.server 8767 --bind 127.0.0.1 --directory desktop/tests/manual
```

Abrir `http://127.0.0.1:8767/before-unload.html`, editar el formulario y
comprobar cancelación y aceptación al cerrar, recargar o navegar.
`http://127.0.0.1:8767/popup.html` permite solicitar `window.open()`: no debe
abrirse otra ventana. Para el cierre durante la carga, usar en su lugar:

```powershell
python desktop/tests/manual/serve-slow-load.py
```

Abrir `http://127.0.0.1:8766/slow.html` y cerrar mientras el recurso sigue
pendiente. Ctrl+C detiene cada servidor. Los
[resultados anteriores y sus límites](docs/implementation-notes.md#confirmación-de-abandono-y-pruebas-manuales-de-cierre)
se conservan como historial, no como ejecución de la nueva guía.

## Diagnóstico local de arranque y carga

Cada proceso principal prepara `%LOCALAPPDATA%\SecureBrowser\Logs`:

| Archivo | Contenido |
| --- | --- |
| `desktop-<milisegundos>-<PID>.log` | Diagnósticos propios enviados a `stderr`; cabecera con configuración, PID y versiones Qt/CEF. |
| `cef-<milisegundos>-<PID>.log` | Log interno de CEF. Puede no existir si el arranque termina antes de prepararlo. |

Los logs se escriben también al abrir el EXE directamente. Consultarlos
**después de cerrar**: el log propio permanece abierto en modo exclusivo
mientras escribe la aplicación. Tras el bloque de ejecución anterior,
localizar el archivo por su PID:

```powershell
$logDirectory = Join-Path $env:LOCALAPPDATA 'SecureBrowser/Logs'
$log = Get-ChildItem -LiteralPath $logDirectory -Filter "desktop-*-$testProcessId.log" |
    Sort-Object LastWriteTimeUtc -Descending |
    Select-Object -First 1
if ($null -eq $log) { throw 'No se ha encontrado el log de esa ejecución.' }
Get-Content -LiteralPath $log.FullName -Encoding UTF8
```

Para una apertura desde el Explorador, anotar el PID mostrado en la cabecera
del log más reciente y contrastar su fecha y configuración. Conservar los
archivos de cada intento al registrar la evidencia.

En un arranque y cierre normales deben aparecer, en orden:

```text
CEF initialized successfully with bootstrap sandbox context.
CEF browser created: id <n>.
CEF browser closing: id <n>.
Qt window destroyed; event loop exit code 0.
CEF shutdown completed.
```

El identificador al crear y cerrar debe coincidir. `OnLoadError` registra el
código del fallo del documento principal; omite iframes y cargas canceladas
`ERR_ABORTED`. El log propio evita incluir la URL y el texto del error.
El log interno CEF puede incluir más detalles: revisar antes de compartirlo.
Los datos internos del perfil están en `%LOCALAPPDATA%\SecureBrowser\CEF`;
no son la persistencia SQLite de SBP y se comparten entre clones y configuraciones.

### Errores anteriores a nuestra entrada y archivos ausentes

Si el bootstrap no consigue cargar la DLL propia o sus dependencias inmediatas,
`RunWinMain` no se ejecuta y puede no existir un log propio. Los recursos CEF
ausentes pueden causar un fallo nativo después de iniciar el registro, sin
que `CefInitialize` llegue a devolver `false`. Revisar la última etapa
registrada, el aviso Windows/bootstrap y el código real.

[`check-startup-failure.ps1`](tests/manual/check-startup-failure.ps1) reproduce
esos casos sobre copias aisladas de la salida:

```powershell
.\desktop\tests\manual\check-startup-failure.ps1 -Scenario ClientDll -Configuration Debug
.\desktop\tests\manual\check-startup-failure.ps1 -Scenario CefResource -Configuration Debug
```

Ejecutar un caso cada vez y cerrar cualquier aviso. El script conserva la copia,
su `bootstrap-stderr.log` y el diagnóstico del PID si existe. El fallo nativo
observado en #92 devolvió `-2147483645` (`0x80000003`); no debe sustituirse por
un `1` supuesto. Los [resultados históricos](docs/implementation-notes.md#diagnósticos-observados-en-92)
distinguen esos casos de un retorno controlado de inicialización.

## Problemas conocidos

| Síntoma | Comprobación o actuación |
| --- | --- |
| No aparece el generador Visual Studio 2022 o CMake rechaza MSVC/x64 | Instalar o completar Build Tools 2022 y v143; usar el generador y arquitectura documentados. |
| El script rechaza la versión de CMake | Repetir `setup-cmake.ps1` en esa terminal y comprobar `cmake --version`. |
| No se encuentra Python o falla aqtinstall | Seleccionar Python 3.10 o posterior con `-PythonExecutable`; comprobar Git y conectividad. Se usa la revisión de aqtinstall del manifiesto. |
| Qt está incompleto o no coincide | Revisar el kit indicado por el diagnóstico: versión, arquitectura, ambas configuraciones, plugins e inventarios. No continuar con una preparación fallida. |
| Falta 7-Zip o la versión no coincide | Instalar 26.05 x64 en la ubicación de requisitos o utilizar explícitamente la alternativa con `tar.exe`. |
| La extracción tarda mucho con `tar.exe` | Utilizar la ruta principal con 7-Zip. No corresponde a compilar el motor CEF. |
| CEF falla por tamaño, SHA-1, versión o arquitectura | Corregir o apartar la descarga/SDK que identifica el diagnóstico y repetir la preparación; se conservan los archivos existentes incorrectos. |
| CMake no encuentra Qt o CEF | Recuperar `$qtKitDirectory` y `$cefRoot`; deben contener el kit y el SDK, respectivamente, no sus carpetas padre. |
| Faltan avisos de Qt o inventarios | Usar los avisos del repositorio para esta versión y un kit con `sbom/` completo. |
| Aparece `Could NOT find WrapVulkanHeaders` | Las cabeceras de desarrollo Vulkan no se necesitan para Foundation; comprobar que siguen `Configuring done` y `Generating done` sin errores. |
| DLL o plugin de plataforma ausente al abrir | Comprobar el inventario y reconstruir `SecureBrowser` con `--clean-first` si se alteró el runtime. Mantener junta la salida completa. |
| SDK presente, pero el comprobador lo rechaza | La carpeta `10.0.28000.0` no demuestra la revisión del paquete; comprobar `10.0.28000.2270` con `setup-windows-sdk.ps1 -CheckOnly`. |
| No se puede leer el log porque está en uso | Cerrar la instancia antes de leer su log. |
| El arranque termina con código `24` | Otra instancia puede estar usando la raíz CEF compartida; cerrar la anterior y repetir de forma secuencial. |
| La web muestra un fallo de DNS (`-105`) o certificado (`-201`) | Consultar el diagnóstico y probar una URL válida. Se mantienen las comprobaciones TLS; no se añaden excepciones para continuar. |

La carga y el primer pintado de sitios con muchos componentes pueden tardar.
Los botones de historial se actualizan consultando la entrada actual; las
marcas `time_ms` de los logs no miden el primer pintado ni garantizan un tiempo
de carga. La carrera exacta de cierre durante `creation_pending_` no se ha
aislado de forma controlada en las pruebas históricas.

Foundation tiene una sola ventana y un navegador, cancela popups y no ofrece
pestañas completas, gestor de descargas, interfaz final ni conexión REST,
login, políticas, filtrado de URL, Security Engine, SQLite o sincronización.
La evidencia de Foundation no acredita esas funcionalidades futuras.

## Organización del código

| Ruta | Responsabilidad |
| --- | --- |
| `src/app/` | Entrada, sesión, lifecycle, navegación y puente entre Qt y CEF. |
| `src/browser/` | Aplicación y cliente CEF, callbacks y consulta del historial. |
| `src/ui/` | Ventana, controles Qt y anfitrión nativo del contenido. |
| `src/diagnostics/` | Preparación de rutas y logs del proceso principal. |
| `ci/` | Herramientas y manifiesto reutilizados por local y Actions. |
| `cmake/` | Inventarios de runtime generados por configuración. |
| `tests/` | Verificaciones de integración y auxiliares manuales. |
| `third_party/qt/6.12.0/` | Avisos comunes de Qt conservados para la versión seleccionada. |

Las clases propias mantienen interfaz pública y auxiliares privados en `.h`,
implementación en `.cpp` y namespace `sbp`. `BrowserSession::Run` compone los
módulos y conserva su vida; los detalles están en las
[notas técnicas](docs/implementation-notes.md#organización-del-código).
`.deps/` y `build/` son salidas locales excluidas de Git.

## Licencias y avisos de terceros

La construcción conserva los avisos del SDK y del kit seleccionados:

| Destino del runtime | Origen |
| --- | --- |
| `README.txt`, `LICENSE.txt`, `CREDITS.html` | Distribución CEF fijada. |
| `qt-notices/licenses/` | Avisos comunes seleccionados por `SBP_QT_LICENSE_DIR`; por defecto, las copias sin modificar del repositorio. |
| `qt-notices/sbom/qtbase-6.12.0.spdx.json` | Inventario SPDX del propio kit Qt. |
| `qt-notices/sbom/qtsvg-6.12.0.spdx.json` | Inventario SPDX del propio kit Qt. |
| `qt-notices/sbom/qttranslations-6.12.0.spdx.json` | Inventario SPDX del propio kit Qt. |

La [procedencia de los avisos comunes](third_party/qt/6.12.0/README.md) se
documenta junto a sus archivos. SBOM significa inventario de componentes;
SPDX es el formato que utiliza Qt. Los inventarios describen el módulo
completo, incluidos componentes que no se despliegan.

Las condiciones de terceros son las que acompañan a cada componente.
Consultar la [documentación de licencias Qt](https://doc.qt.io/qt-6/licensing.html)
y su [inventario de terceros](https://doc.qt.io/qt-6/licenses-used-in-qt.html).
La preparación conserva esos materiales; no constituye una entrega pública
ni un instalador, que quedan fuera de Foundation.

## Evidencia y finalización de la guía

El [registro de validación](docs/clean-clone-validation.md) diferencia:

- Las verificaciones y pruebas visuales ya registradas para #93.
- El CI limpio, con caché y posterior al merge de #94.
- La ejecución literal de esta guía desde un clon limpio para #95, **pendiente**.

Completar el último registro con la revisión probada, entorno, resultados
Debug/Release y diagnósticos reales. Una ejecución en el equipo o clon de
desarrollo anterior no sustituye esa comprobación. El
[CI de Desktop](ci/README.md) comprueba compilación, runtime y arranque/cierre;
la validación visual se realiza en Windows 11.
