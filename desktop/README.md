# SecureBrowser Desktop

## Estado actual

El ejecutable temporal `sbp_toolchain_check` abre una ventana Qt con el título
**SecureBrowser** y un tamaño inicial de 1024 por 768 unidades lógicas.
`QApplication` inicializa Qt Widgets y procesa los eventos; un `QWidget`
sin padre actúa como ventana independiente. Al cerrar la última ventana,
termina el bucle de eventos y el proceso devuelve su código de salida.
El target `SecureBrowser` construye nuestra DLL (Dynamic-Link Library,
biblioteca de enlace dinámico), enlazada con Qt y CEF (Chromium Embedded
Framework). Junto a ella copia el bootstrap oficial como `SecureBrowser.exe`.
La construcción prepara también los binarios y recursos CEF, las dependencias
Qt, los avisos de terceros y los permisos de la carpeta de ejecución.
La entrada `RunWinMain` de [`app_entry_win.cpp`](app_entry_win.cpp) es mínima
y devuelve `0`; la inicialización de CEF se implementará en #85.
La preparación independiente del SDK CEF está disponible mediante
[`setup-cef.ps1`](../scripts/setup-cef.ps1).

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
2. Compilar `app_entry_win.cpp` y enlazar `SecureBrowser.dll` con Qt Widgets,
   el adaptador, el `libcef.lib` correspondiente y las bibliotecas de Windows
   declaradas por CEF.
3. Copiar el bootstrap de esa configuración como `SecureBrowser.exe`, los
   binarios de `CEF_BINARY_FILES` y los recursos de `CEF_RESOURCE_FILES`.
   Ambas listas proceden de `cef_variables.cmake`; `COPY_FILES` y
   `COPY_SINGLE_FILE` proceden de `cef_macros.cmake`.
4. Ejecutar `Qt6::windeployqt`, herramienta del kit seleccionado, con su
   `Qt6::qtpaths`, modo Debug o Release explícito y destino junto a la DLL.
   Se analizan `SecureBrowser.dll` y `sbp_toolchain_check.exe`: la entrada
   mínima de nuestra DLL todavía no utiliza funciones Qt y el ejecutable
   temporal aporta esas dependencias durante esta etapa.
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
La preparación de la carpeta no inicializa CEF. `SecureBrowser.exe` utiliza
todavía una entrada que devuelve `0`, sin abrir una ventana. La inicialización
CEF y los subprocesos se implementarán en #85. La ventana disponible se
comprueba con el ejecutable temporal, como se explica a continuación.

## Compilar y ejecutar la ventana Qt de comprobación

Construir el target `SecureBrowser` para preparar también las dependencias.
Después, ejecutar la ventana temporal sin añadir Qt al `PATH` (variable de
entorno de búsqueda de ejecutables). Desde la raíz del repositorio:

```powershell
cmake --build desktop/build --config Debug --target SecureBrowser --parallel 4
.\desktop\build\Debug\sbp_toolchain_check.exe
$LASTEXITCODE
```

Desde la raíz del repositorio, repetir para Release en la misma terminal:

```powershell
cmake --build desktop/build --config Release --target SecureBrowser --parallel 4
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
la ejecución CEF con sandbox se comprobará al implementar su inicialización.

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
