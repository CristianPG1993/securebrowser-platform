# SecureBrowser Desktop

## Estado actual

`sbp_toolchain_check` es un ejecutable temporal para comprobar la cadena de
herramientas C++. Su entrada `main` devuelve `0`, sin mostrar texto ni abrir
ventanas. La tarea #80 incorpora la localización de Qt y el enlace con
`Qt6::Widgets`. Todavía no se utilizan clases Qt ni se integra CEF
(Chromium Embedded Framework).

## Requisitos

- Windows 11 x64.
- CMake 3.29 o superior; versión de referencia: 3.29.2.
- Visual Studio Build Tools 2022 con las herramientas C++ MSVC
  (Microsoft Visual C++) v143 para x64.
- Windows SDK (Software Development Kit, kit de desarrollo de software),
  paquete `10.0.28000.2270`. El identificador de instalación utilizado por
  CMake es `10.0.28000.0`.
- Qt 6.12.0, kit `msvc2022_64`, con las bibliotecas Debug y Release.

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

## Configurar

Ejecutar en PowerShell desde la raíz del repositorio:

```powershell
cmake -S desktop -B desktop/build -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143 "-DQt6_DIR=C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6"
```

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

## Compilar y ejecutar

Debug:

```powershell
cmake --build desktop/build --config Debug --target sbp_toolchain_check
.\desktop\build\Debug\sbp_toolchain_check.exe
$LASTEXITCODE
```

Release:

```powershell
cmake --build desktop/build --config Release --target sbp_toolchain_check
.\desktop\build\Release\sbp_toolchain_check.exe
$LASTEXITCODE
```

Comprobar que cada comando de CMake termina sin errores antes de continuar.
El código de salida inmediatamente después de ejecutar cada programa debe
ser `0`. El programa no produce salida visible.

`--config` selecciona la configuración y `--target` el ejecutable que se
construye. El generador Visual Studio admite ambas configuraciones dentro
de una misma carpeta de construcción, con salidas separadas en `Debug/`
y `Release/`; no se utiliza `CMAKE_BUILD_TYPE` para elegir entre ellas.

El target requiere C++20 sin extensiones del compilador. Utiliza la CRT
(C Run-Time Library, biblioteca de ejecución de C) dinámica: `/MDd` en Debug
y `/MD` en Release, mediante `CMAKE_MSVC_RUNTIME_LIBRARY`.

## Dependencias de desarrollo y ejecución

| Elementos del kit Qt | Uso |
| --- | --- |
| `include/`, `lib/cmake/` y bibliotecas de enlace `.lib` en `lib/` | Configurar y compilar en el equipo de desarrollo. |
| `bin/windeployqt.exe` | Preparar las dependencias Qt de una salida de ejecución. Se utiliza desde el mismo kit. |
| `Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll` | Bibliotecas Release necesarias cuando la aplicación utiliza esos módulos. Las variantes Debug llevan sufijo `d`. |
| `plugins/platforms/qwindows.dll` y su variante Debug `qwindowsd.dll` | Plugin de plataforma para la futura ventana Qt, situado en `platforms/` dentro de la salida. |

La copia de las DLL (Dynamic-Link Library, bibliotecas de enlace dinámico),
plugins y dependencias adicionales se implementará en #84 conforme al
ADR-0011 y a la
[guía de distribución en Windows](https://doc.qt.io/qt-6/windows-deployment.html).
El SDK de desarrollo completo no forma parte de la carpeta de ejecución.

El `main` actual no llama a funciones Qt. Su salida `0` comprueba el programa
mínimo, pero todavía no acredita la carga de bibliotecas Qt ni del plugin
de plataforma. La ventana y su ejecución se comprobarán en #81.

La instalación Qt queda fuera del repositorio; `build/`, `CMakeCache.txt`
y `CMakeUserPresets.json` están excluidos por `.gitignore`. Solo se versionan
las fuentes, la configuración del proyecto y las instrucciones.

## Licencias y avisos de terceros

Las condiciones de Qt dependen de los módulos y de la opción de licencia
utilizada. Las referencias son la
[documentación de licencias Qt](https://doc.qt.io/qt-6/licensing.html) y el
[inventario de código de terceros](https://doc.qt.io/qt-6/licenses-used-in-qt.html).
El kit instalado también contiene el directorio `sbom/` con el inventario
de componentes y sus licencias.

La preparación de la distribución en #84 conservará los textos de licencia
y avisos correspondientes a los componentes realmente redistribuidos,
conforme al ADR-0011. Esta tarea prepara el entorno de desarrollo; todavía
no genera un paquete para entregar a otros equipos.
