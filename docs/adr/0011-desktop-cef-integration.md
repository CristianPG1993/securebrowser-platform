# ADR-0011: Entorno e integración CEF de Desktop

- Estado: Aceptada
- Fecha: 2026-10-24
- Fecha de aceptación: 2026-10-24

## Contexto

Desktop Foundation necesita una base de herramientas y dependencias concreta
antes de configurar CMake e integrar el navegador. La GUI (Graphical User
Interface, interfaz gráfica de usuario) utiliza Qt 6 Widgets, conforme al
[ADR-0002](0002-desktop-gui.md), y CEF (Chromium Embedded Framework) carga y
renderiza las páginas.

Este ADR (Architecture Decision Record, registro de decisión arquitectónica)
recoge las decisiones revisadas y aprobadas por el usuario en la tarea #78.
Su aceptación fija la base técnica de Desktop Foundation. La preparación de
dependencias, la compilación y la ejecución se verificarán en las tareas de
implementación correspondientes, como se detalla en la sección de validación.

## Opciones consideradas

Qt ofrece distintos kits según compilador y arquitectura. Se selecciona el kit
MSVC (Microsoft Visual C++) 2022 de 64 bits para utilizar las herramientas de
Visual Studio Build Tools 2022 ya instaladas y la configuración Windows x64
documentada por Qt.

Para generar el proyecto se utiliza Visual Studio 17 2022, disponible en la
versión de CMake instalada. Ninja también está instalado, pero no forma parte
de esta base de construcción aprobada.

Para coordinar los eventos de Qt y CEF se consideran dos alternativas:

| Alternativa | Ventaja | Coste |
| --- | --- | --- |
| Integrar CEF en el bucle Qt mediante `CefDoMessageLoopWork`. | Comparte el hilo principal para ambos bucles. | Requiere planificar el procesamiento CEF y controlar temporizadores y reentradas. |
| Ejecutar el bucle CEF en un hilo propio. | CEF gestiona su procesamiento de eventos. | Requiere comunicación explícita entre hilos y coordinación del cierre. |

Para alojar el contenido web se consideran estas alternativas:

| Alternativa | Ventaja | Coste |
| --- | --- | --- |
| Ventana nativa CEF hija de un widget Qt. | CEF gestiona el dibujo del contenido en su ventana. | Requiere coordinar tamaño, foco y cierre con Qt. |
| OSR (Off-Screen Rendering, renderizado fuera de pantalla). | Permite integrar el contenido mediante dibujo propio. | Requiere gestionar la presentación del contenido y el envío de eventos de entrada. |

## Decisión

### Herramientas y versiones aprobadas

| Elemento | Base de Desktop Foundation |
| --- | --- |
| Plataforma inicial | Windows 11 x64. |
| Qt | 6.12.0, kit `msvc2022_64`, con Qt Widgets. |
| Compilador | MSVC v143 de Visual Studio Build Tools 2022. |
| Windows SDK (Software Development Kit, kit de desarrollo de software) | Paquete `10.0.28000.2270`. |
| Lenguaje | C++20. |
| CMake | 3.29.2 como versión de referencia inicial. |
| Generador CMake | `Visual Studio 17 2022`, arquitectura `x64`. |
| CEF | `156.0.14+g4fa4ab5+chromium-156.0.8078.48`, distribución Standard Windows x64. |

Las dependencias se seleccionarán explícitamente para evitar que la
configuración tome el Qt 5.15.2 de Anaconda encontrado en el `PATH` local
(variable de entorno de búsqueda de ejecutables).
Las rutas de instalación se proporcionarán mediante configuración local;
no se versionarán rutas personales.

### Obtención y localización del SDK CEF

La preparación será un paso explícito mediante un script PowerShell,
implementado en la tarea #82. El script descargará el paquete fijado, comprobará
su integridad antes de extraerlo y reutilizará una descarga válida.

Los metadatos consultados para la distribución Standard Windows x64 son:

| Dato | Valor fijado |
| --- | --- |
| Archivo | `cef_binary_156.0.14+g4fa4ab5+chromium-156.0.8078.48_windows64.tar.bz2` |
| Origen | `https://cef-builds.spotifycdn.com/` |
| Tamaño | 367176591 bytes. |
| SHA-1 (Secure Hash Algorithm 1), publicado por CEF | `7a1049f0facf3b6ce9efc4d683cf1bac72691ba6` |

Se versionará un manifiesto con la versión, arquitectura, tipo de paquete,
nombre del archivo, URL (Uniform Resource Locator, localizador de recursos)
completa y hash esperado. El script utilizará esos valores sin seleccionar
automáticamente otra versión.

La ubicación local predeterminada del SDK será
`desktop/.deps/cef/<directorio-del-paquete>/`, conservando el directorio raíz
contenido en el archivo. Las descargas y el SDK se mantendrán dentro de
`desktop/.deps/`; su exclusión en `.gitignore` se incorporará en la tarea #82.

CMake recibirá `CEF_ROOT` como variable de configuración, apuntando al
directorio que contiene `include/`, `cmake/`, `Debug/`, `Release/` y
`Resources/`. Se utilizará `FindCEF.cmake` de ese mismo SDK, añadiendo su
directorio `cmake/` a `CMAKE_MODULE_PATH` antes de `find_package(CEF REQUIRED)`.
También se admitirá un SDK preparado fuera del repositorio mediante esa raíz
explícita, sin versionar rutas personales.

La configuración comprobará la versión, la arquitectura y los archivos
necesarios. Si el SDK falta o no corresponde, indicará cómo prepararlo o
corregir `CEF_ROOT`. La descarga se realizará mediante el script de preparación,
sin iniciarla automáticamente al configurar el proyecto.

Se versionarán el script, el manifiesto, la configuración CMake y la
documentación. Los archivos comprimidos, las bibliotecas precompiladas,
los recursos descargados y las salidas de construcción quedarán fuera de Git.

### Windows SDK

Se adopta el paquete Windows SDK `10.0.28000.2270` para alinear esta herramienta
con el entorno de compilación publicado para CEF 156, rama 8078.

La tabla de CEF describe la construcción de Chromium/CEF desde sus fuentes.
Desktop utilizará la distribución binaria Standard y compilará su propio
código y `libcef_dll_wrapper` mediante CMake. La elección del SDK es una
referencia de entorno; la integración completa sigue pendiente de validación.

Al verificar la instalación se registrará también el identificador de SDK
que detecta CMake, distinguiéndolo de la versión del paquete descargado.

### CRT (C Run-Time Library, biblioteca de ejecución de C)

Se utiliza el CRT dinámico para el código propio y el adaptador oficial de
CEF, con una configuración coherente con el kit Qt MSVC seleccionado:

| Código que compilamos | Debug | Release |
| --- | --- | --- |
| `SecureBrowser.dll` | `/MDd` | `/MD` |
| `libcef_dll_wrapper` | `/MDd` | `/MD` |

`/MD` utiliza el runtime mediante DLL (Dynamic-Link Library, biblioteca de
enlace dinámico) y `/MDd` selecciona su variante de depuración. La alternativa
`/MT` incorpora el runtime estáticamente en cada módulo. El CRT dinámico se
adopta para mantener compatibles los objetos que enlazamos y la configuración
habitual de Qt para MSVC.

La distribución CEF seleccionada define `CEF_RUNTIME_LIBRARY_FLAG` como `/MT`
por defecto. Se configurará como `/MD` antes de cargar la configuración CEF;
sus reglas añaden la variante de depuración cuando corresponde. Los targets
propios seleccionarán el mismo runtime mediante `MSVC_RUNTIME_LIBRARY`.

`libcef_dll_wrapper` seguirá siendo una biblioteca estática `.lib`, aunque
utilice el CRT dinámico. Esta elección no recompila ni modifica el bootstrap
o `libcef.dll`, que se obtienen precompilados.

### Arranque con bootstrap y procesos CEF

Se utiliza el bootstrap oficial de la distribución CEF fijada, conservando
el sandbox (aislamiento de acceso a recursos del sistema de los subprocesos).
La salida tendrá las siguientes responsabilidades:

| Archivo | Responsabilidad |
| --- | --- |
| `SecureBrowser.exe` | Copia renombrada de `bootstrap.exe` de CEF, correspondiente a la configuración Debug o Release. |
| `SecureBrowser.dll` | Aplicación propia C++/Qt; exporta la entrada `RunWinMain`. |
| `libcef.dll` | Motor CEF precompilado de la misma versión y configuración. |

Al arrancar, el bootstrap carga nuestra DLL en el mismo proceso y llama a
`RunWinMain`, pasando los argumentos y el contexto de sandbox. Cargar la DLL
no crea por sí mismo otro proceso.

El punto de entrada seguirá esta secuencia:

1. Preparar `CefMainArgs` y llamar a `CefExecuteProcess`, pasando el contexto
   de sandbox, antes de crear `QApplication` u otros objetos Qt.
2. Si `CefExecuteProcess` devuelve un valor mayor o igual que cero, devolver
   ese código y terminar el subproceso sin crear la interfaz Qt.
3. Si devuelve un valor negativo, continuar como proceso principal: crear
   `QApplication` y configurar e inicializar CEF con el contexto de sandbox
   recibido y las opciones de eventos acordadas.
4. Si `CefInitialize` devuelve false, terminar con `CefGetExitCode`, tanto
   ante un fallo como ante una salida anticipada prevista por CEF. No llamar
   a `CefShutdown` cuando la inicialización no continúa.
5. Tras una inicialización correcta, ejecutar el bucle Qt. El browser se
   creará cuando CEF notifique que su contexto está inicializado. El apagado
   seguirá la coordinación asíncrona descrita en la sección de eventos.

CEF reutilizará `SecureBrowser.exe` para el proceso principal y los subprocesos
de renderizado, GPU (Graphics Processing Unit, unidad de procesamiento gráfico)
y otros servicios. No se configurará un ejecutable alternativo mediante
`browser_subprocess_path`. El hilo propio del bucle CEF pertenece al proceso
principal y no sustituye a estos subprocesos.

La aplicación requerirá el contexto de sandbox del bootstrap para continuar;
su ausencia se diagnosticará sin desactivar el aislamiento automáticamente.
La inicialización de Qt y CEF se realizará desde la entrada exportada, sin
introducirla en `DllMain` ni en constructores globales.

### Coordinación de eventos de Qt y CEF

Se adopta `multi_threaded_message_loop = true` y se mantiene
`external_message_pump = false`. En el proceso principal, Qt ejecutará
`QApplication::exec` en el hilo principal y CEF gestionará su bucle en un hilo
de UI (User Interface, interfaz de usuario) propio.

La decisión prioriza el alcance Windows y el tiempo disponible para el TFG. Permite delegar en CEF la planificación de su bucle,
manteniendo explícito el coste de comunicar ambos hilos. No representa una
mejora de rendimiento medida.

Se respetarán las siguientes responsabilidades:

- Crear y utilizar los widgets Qt únicamente en el hilo principal.
- Ejecutar `CefInitialize` y `CefShutdown` en el hilo principal, aunque el
  bucle CEF tenga su propio hilo.
- Enviar las notificaciones CEF hacia Qt mediante conexiones encoladas
  (`Qt::QueuedConnection`), con receptores cuya vida esté controlada.
- Enviar las operaciones que exijan el hilo de interfaz CEF mediante
  `CefPostTask` hacia `TID_UI`, su identificador de hilo. Los demás callbacks
  respetarán el hilo indicado por su API (Application Programming Interface,
  interfaz de programación de aplicaciones).
- Utilizar el bucle de Qt como bucle principal de la aplicación, sin llamar
  a `CefRunMessageLoop` ni a `CefDoMessageLoopWork` en este modo.

El cierre será asíncrono: se solicitará cerrar los browsers y se mantendrá el
procesamiento de eventos hasta recibir sus callbacks `OnBeforeClose` y liberar
las referencias correspondientes. Después se notificará al hilo Qt para
finalizar su bucle y ejecutar `CefShutdown` en el hilo principal. Se evitarán
esperas bloqueantes entre ambos hilos y accesos a widgets ya destruidos.

### Alojamiento nativo del browser y estilo Alloy

Se adopta una ventana nativa CEF hija de un `QWidget` anfitrión. Qt construirá
la ventana principal y sus controles, reservando ese widget para el área de
contenido web. CEF gestionará el renderizado dentro de su propia ventana hija.

El browser utilizará explícitamente `CEF_RUNTIME_STYLE_ALLOY`, definido en
`CefWindowInfo.runtime_style`. Este estilo permite utilizar el contenido CEF
con los controles propios de Qt. Se mantiene la distribución y el bootstrap
acordados; Alloy es un estilo del browser de esa misma distribución.

Se utilizará renderizado con ventana, manteniendo
`windowless_rendering_enabled = false`. La alternativa OSR queda fuera de
Foundation para limitar el trabajo de dibujo y tratamiento de entrada propios.

La integración seguirá estas responsabilidades:

- Crear el widget anfitrión y obtener su identificador de ventana nativa
  mediante `QWidget::winId`, siempre desde el hilo Qt.
- Pasar el identificador y la geometría necesaria al código CEF, sin acceder
  a widgets desde su hilo de interfaz.
- Configurar la relación de ventana hija mediante `CefWindowInfo::SetAsChild`
  y crear el browser de forma asíncrona cuando existan tanto el anfitrión
  nativo como el contexto CEF inicializado.
- Coordinar los cambios de tamaño, visibilidad y foco con el browser,
  respetando el hilo requerido por cada operación.
- Conservar el widget y su ventana nativa hasta confirmar el cierre del
  browser mediante `OnBeforeClose`, antes de destruir el anfitrión.

Se comprobará la vigencia del identificador nativo y la correspondencia de
coordenadas al cambiar el escalado de Windows. El contenido web se mantendrá
en su área reservada; esta decisión no incorpora la interfaz completa.

### Inventario del SDK y distribución de ejecución

Este inventario procede de las instrucciones y plantillas del commit CEF
fijado. Se contrastará con el contenido del paquete al prepararlo en #82;
todavía no acredita una extracción ni una ejecución local del SDK.

Para configurar y compilar se utilizarán los siguientes elementos:

| Elemento del SDK | Uso |
| --- | --- |
| `include/` | Cabeceras de la API CEF e información de versión. |
| `cmake/` | Módulos `FindCEF.cmake`, `cef_variables.cmake` y `cef_macros.cmake`. |
| `libcef_dll/` | Fuentes para compilar `libcef_dll_wrapper` con el CRT acordado. |
| `Debug/` y `Release/` | `libcef.lib` para el enlace, `bootstrap.exe` y binarios de cada configuración. |
| `Resources/` | Datos, paquetes de recursos y locales necesarios para la ejecución. |
| `README.txt`, `LICENSE.txt` y `CREDITS.html` | Instrucciones de la distribución, licencia y avisos de terceros. |

Además del bootstrap renombrado y la DLL propia, la salida de ejecución
incluirá los siguientes archivos de CEF para Windows x64:

| Grupo | Archivos |
| --- | --- |
| Motor y soporte de fallos | `libcef.dll`, `chrome_elf.dll`. |
| Datos Unicode y V8 | `icudtl.dat`, `v8_context_snapshot.bin`. |
| Recursos | `chrome_100_percent.pak`, `chrome_200_percent.pak`, `resources.pak` y el directorio `locales/`. |
| Soporte gráfico | `d3dcompiler_47.dll`, `dxcompiler.dll`. |
| Renderizado por software | `vk_swiftshader.dll`, `vk_swiftshader_icd.json`, `vulkan-1.dll`. |

Las listas `CEF_BINARY_FILES` y `CEF_RESOURCE_FILES` del SDK fijado serán la
referencia para la copia. Foundation conservará también los componentes que
su README describe como opcionales para evitar desactivar funciones de dibujo
o recursos al reducir prematuramente la distribución. Se mantendrá completo
el directorio de locales suministrado.

El bootstrap se copiará por separado desde la configuración correspondiente,
como hace el ejemplo oficial `cefsimple`. Las bibliotecas de enlace
`libcef.lib` y `libcef_dll_wrapper.lib` se utilizan al construir la DLL propia;
no se necesitan como archivos independientes para ejecutar la aplicación.

La salida incorporará también las dependencias Qt del kit seleccionado:
`Qt6Core.dll`, `Qt6Gui.dll`, `Qt6Widgets.dll` y `platforms/qwindows.dll` en
Release, o sus variantes con sufijo `d` en Debug. La herramienta `windeployqt`
del mismo kit detectará los plugins y dependencias adicionales pertinentes,
analizando `SecureBrowser.dll`, que contiene el código Qt, con la configuración
Debug o Release explícita y el directorio de destino indicado.

Se prepararán salidas separadas en `desktop/build/Debug/` y
`desktop/build/Release/`, manteniendo cada bootstrap, DLL, biblioteca y recurso
de la versión y configuración correspondientes. Los archivos CEF y sus recursos
se colocarán junto al ejecutable, conservando los subdirectorios `locales/` y
los plugins Qt. Las rutas de recursos se resolverán desde la ubicación de la
aplicación, sin depender del directorio de trabajo del usuario.

La preparación aplicará las reglas de permisos del SDK necesarias para que
los subprocesos con sandbox puedan leer y ejecutar los archivos de la salida.
Se conservarán la licencia CEF, los créditos y los avisos de los componentes
redistribuidos, junto con los correspondientes a Qt. El runtime de Microsoft
Visual C++ compatible será un requisito de la ejecución Release.

La copia de binarios, recursos, dependencias y avisos se implementará en #84.
Esta decisión define una carpeta de ejecución; el instalador, la firma y la
publicación quedan fuera de este bloque.

## Consecuencias

- El proyecto parte de una combinación concreta de Qt, compilador y arquitectura.
- CMake debe seleccionar el generador y la arquitectura acordados explícitamente.
- Se necesita preparar el kit Qt 6 indicado; el inventario local todavía no
  acredita su instalación.
- Las configuraciones Debug y Release se comprobarán durante el desarrollo.
- La ejecución Release requiere un runtime de Microsoft Visual C++ compatible;
  su disponibilidad se comprobará al preparar la distribución.
- La integración debe trasladar las notificaciones y operaciones al hilo que
  corresponda, conservando la vida de los objetos hasta terminar el cierre.
- La salida necesita tanto el bootstrap como la DLL propia y el runtime CEF;
  el arranque de subprocesos debe resolverse antes de inicializar Qt.
- Las actualizaciones de Qt y CEF se revisarán de forma explícita.
- La preparación del SDK CEF y la configuración del proyecto serán pasos
  separados y repetibles; los binarios descargados no se incorporarán a Git.
- El alojamiento inicial utiliza ventanas nativas de Windows; su tamaño,
  foco y vida deben coordinarse con el widget anfitrión Qt.
- La ejecución necesita una carpeta preparada con sus recursos y dependencias,
  separada por configuración y comprobada fuera del entorno del IDE
  (Integrated Development Environment, entorno de desarrollo integrado).

## Validación

La revisión inicial ha identificado Windows 11 de 64 bits, Visual Studio Build
Tools 2022 con MSVC 14.44.35207, CMake 3.29.2 y Ninja 1.12.0. El Qt encontrado
en el PATH es 5.15.2 y pertenece a Anaconda.

Se ha comprobado documentalmente la disponibilidad de Qt 6.12.0 con el kit
MSVC 2022 de 64 bits y del generador CMake elegido.

El usuario confirma que ha descargado el Windows SDK `10.0.28000.2270`.
La comprobación local todavía no identifica la instalación del SDK 28000 en
Windows Kits. Quedan pendientes verificar su instalación y la selección
efectiva del SDK durante la configuración con CMake.

Se han consultado los metadatos oficiales del paquete CEF fijado, incluidos
nombre, tamaño y SHA-1. El SDK todavía no se ha descargado ni se ha verificado
su contenido local. En #82 se comprobarán la integridad del archivo, una
preparación inicial y su repetición; en #83 se comprobará la configuración
con una raíz válida y el rechazo de SDK ausente, incorrecto o incompleto.

Al compilar se comprobarán las opciones efectivas de CRT en el código propio
y `libcef_dll_wrapper`, evitando mezclar `/MT` y `/MD` o variantes Debug y
Release dentro de los objetos que se enlazan juntos. También se verificará
la correspondencia con las bibliotecas Qt de cada configuración.

Se comprobarán la entrada exportada, el paso del contexto de sandbox y el
recorrido principal/subproceso. Se verificará la carga de las dependencias
Qt y que no se inicialicen objetos Qt antes de resolver el tipo de proceso.
También se comprobarán los resultados de inicialización y salida anticipada,
sin tratar como fallo una salida normal indicada por CEF.

Durante la integración se comprobarán los hilos de inicialización, callbacks,
acceso a widgets y apagado. Las pruebas de respuesta de la interfaz y cierre
durante creación o carga verificarán la coordinación asíncrona elegida.

Se verificará que el browser utiliza el estilo Alloy y permanece embebido en
el área Qt prevista. Se probarán redimensionamiento, foco, minimizar/restaurar,
escalado de Windows y cierre durante la creación o carga, comprobando que el
anfitrión permanece válido hasta completar el cierre del browser.

En #84 se comparará el inventario preparado con las listas del SDK y con las
dependencias Qt detectadas. Se comprobarán las salidas Debug y Release desde
otro directorio de trabajo y, para la entrega, sin depender de rutas del IDE
o del SDK en el PATH. Se revisarán recursos, locales, plugins, avisos y permisos
de lectura y ejecución del sandbox; un recurso ausente o una mezcla de
configuraciones se verificará sobre una copia temporal de la salida.

Todavía no se ha compilado ni ejecutado una aplicación Qt/CEF. La validación
posterior comprobará la configuración, compilación y ejecución en Debug y
Release, además de la coordinación de eventos, el renderizado y el cierre.

## Referencias

- [Tarea #78: entorno e integración CEF](https://github.com/CristianPG1993/securebrowser-platform/issues/78).
- [ADR-0002: selección de la interfaz Desktop](0002-desktop-gui.md).
- [Qt 6.12.0: kits oficiales para Windows](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/).
- [Qt: plataformas compatibles](https://doc.qt.io/qt-6/supported-platforms.html).
- [CMake 3.29: generador Visual Studio 17 2022](https://cmake.org/cmake/help/v3.29/generator/Visual%20Studio%2017%202022.html).
- [CEF: ramas y requisitos de construcción](https://chromiumembedded.github.io/cef/branches_and_building.html).
- [Microsoft: descargas del Windows SDK](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads).
- [Microsoft: opciones de biblioteca de ejecución](https://learn.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library?view=msvc-170).
- [CMake 3.29: MSVC_RUNTIME_LIBRARY](https://cmake.org/cmake/help/v3.29/prop_tgt/MSVC_RUNTIME_LIBRARY.html).
- [CEF seleccionado: configuración del CRT](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/cmake/cef_variables.cmake.in).
- [Qt 6.12.0: configuración MSVC](https://github.com/qt/qtbase/blob/v6.12.0/mkspecs/common/msvc-desktop.conf).
- [CEF: integración del bucle de eventos](https://chromiumembedded.github.io/cef/general_usage.html#message-loop-integration).
- [CEF seleccionado: funciones de inicialización y bucles](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/include/cef_app.h).
- [Qt: hilos y objetos](https://doc.qt.io/qt-6/threads-qobject.html).
- [CEF: sandbox y bootstrap en Windows](https://chromiumembedded.github.io/cef/sandbox_setup.html).
- [CEF seleccionado: ejemplo de entrada Windows](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/tests/cefsimple/cefsimple_win.cc).
- [CEF seleccionado: contrato de entrada del bootstrap](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/include/cef_sandbox_win.h).
- [CEF: distribuciones binarias](https://cef-builds.spotifycdn.com/index.html).
- [CEF: metadatos oficiales de las distribuciones](https://cef-builds.spotifycdn.com/index.json).
- [CEF seleccionado: localización mediante FindCEF.cmake](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/cmake/FindCEF.cmake.in).
- [Qt: identificador nativo de QWidget](https://doc.qt.io/qt-6/qwidget.html#winId).
- [CEF seleccionado: configuración de ventana hija](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/include/internal/cef_win.h).
- [CEF seleccionado: estilos de browser](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/include/internal/cef_types_runtime.h).
- [CEF seleccionado: contenido de la distribución Standard](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/tools/distrib/win/README.standard.txt).
- [CEF seleccionado: componentes redistribuibles](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/tools/distrib/win/README.redistrib.txt).
- [CEF seleccionado: copia de runtime del ejemplo Windows](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/tests/cefsimple/CMakeLists.txt.in).
- [CEF seleccionado: avisos de licencia y créditos](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/tools/distrib/README.footer.txt).
- [Qt: despliegue en Windows](https://doc.qt.io/qt-6/windows-deployment.html).
- [Qt 6.12.0: herramienta windeployqt](https://github.com/qt/qtbase/blob/v6.12.0/src/tools/windeployqt/main.cpp).
