# Verificación de Desktop

Esta guía recoge la verificación de Desktop Foundation de la
[tarea #93](https://github.com/CristianPG1993/securebrowser-platform/issues/93)
y las pruebas unitarias del Security Engine iniciadas en la
[tarea #115](https://github.com/CristianPG1993/securebrowser-platform/issues/115).
Ejecutar los comandos desde la raíz del repositorio, con Windows PowerShell 5.1
o PowerShell en Windows. La preparación de MSVC, Qt, CEF e ICU y los comandos de
construcción están en el [README de Desktop](../README.md).

## Pruebas unitarias del Security Engine

Estas pruebas requieren CMake 3.29 o posterior, MSVC 2022 x64 con el toolset
v143 y el SDK de Windows 10.0.28000.0 utilizado por los comandos siguientes.
El núcleo y las pruebas se configuran sin buscar Qt ni CEF.

Preparar también el SDK de ICU4C con la versión y la integridad fijadas en
[icu-sdk.json](../icu-sdk.json):

```powershell
.\scripts\setup-icu.ps1
```

El script prepara ICU `76.1`, Unicode `16.0`, en
`desktop/.deps/icu/icu4c-76_1-Win64-MSVC2022`. CMake lo utiliza desde esa
ubicación por defecto. Si se prepara en otro destino, añadir
`-DSBP_ICU_ROOT="<ruta del SDK preparado>"` al comando de configuración.
La dependencia de ICU también se exige con la aplicación desactivada.

Preparar GoogleTest con la versión y la integridad fijadas en
[googletest.json](../googletest.json):

```powershell
.\scripts\setup-googletest.ps1
```

El script prepara las fuentes en `desktop/.deps/googletest/googletest-1.18.0`.
CMake utiliza esas fuentes locales; no descarga dependencias al configurar.
Para utilizar una copia preparada en otra ubicación, añadir
`-DSBP_GOOGLETEST_ROOT="<ruta de las fuentes>"` al comando de configuración.

Configurar una carpeta de compilación independiente de la aplicación:

```powershell
cmake -S desktop -B desktop/build/tests -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143 -DSBP_BUILD_DESKTOP_APP=OFF -DBUILD_TESTING=ON
```

Compilar y ejecutar en Debug:

```powershell
cmake --build desktop/build/tests --config Debug --target sbp_security_tests --parallel 4
ctest --test-dir desktop/build/tests -C Debug --output-on-failure --no-tests=error --timeout 60
```

Repetir en Release utilizando la misma carpeta del generador de Visual Studio:

```powershell
cmake --build desktop/build/tests --config Release --target sbp_security_tests --parallel 4
ctest --test-dir desktop/build/tests -C Release --output-on-failure --no-tests=error --timeout 60
```

La compilación copia `icuuc76.dll`, `icudt76.dll` y `icu-notices/` junto a
`sbp_security_tests.exe`, tanto en Debug como en Release. Los avisos incluyen
la licencia y una copia del manifiesto ICU. No es necesario añadir el SDK
al `PATH` para ejecutar las pruebas.

CTest descubre cada prueba de GoogleTest antes de ejecutarla, incluidos los
casos parametrizados. Actualmente se esperan 275 casos:

- [security_decision_test.cpp](security/security_decision_test.cpp): 14 casos,
  uno para `Allowed` y trece para los motivos de bloqueo. Comprueban la acción
  obtenida y la conservación del motivo recibido por el constructor.
- [url_filtering_config_test.cpp](security/url_filtering_config_test.cpp): tres
  casos que comprueban la conservación del modo y las reglas al construir una
  configuración desactivada, una `ALLOWLIST` vacía y la independencia respecto
  a cambios posteriores en el vector de origen.
- [download_control_config_test.cpp](security/download_control_config_test.cpp):
  tres casos equivalentes para la configuración de extensiones de descarga.
- [policy_snapshot_test.cpp](security/policy_snapshot_test.cpp): tres casos que
  comprueban identificadores de 64 bits, nombre, fechas con microsegundos,
  conservación separada de ambas configuraciones e independencia respecto
  a cambios posteriores en los objetos de entrada.
- [policy_manager_test.cpp](security/policy_manager_test.cpp): 160 casos.
  Los 109 casos de validación mediante `PolicyManager::Validate` incluyen una
  política válida, cinco identidades inválidas o ajenas, 23 nombres, ocho combinaciones de modos,
  24 configuraciones de dominios, 25 configuraciones básicas de extensiones,
  20 configuraciones de extensiones Unicode y tres prioridades entre errores.
  Se comprueban listas vacías, límites de longitud, formato y duplicados
  normalizados, también con las protecciones desactivadas. Las pruebas de
  reglas comprueban además que validar conserva los datos originales del snapshot.
  Los otros 21 casos cubren `BeginContext`, `IsCurrentContext` y `EndContext`: creación y
  sustitución de contextos, IDs inválidos, conservación de generaciones y del
  contexto actual ante intentos rechazados, ausencia de estado y correspondencia
  de generación, dispositivo y compañía. Incluyen cierre correcto, cierre
  repetido, reapertura sin reutilizar generación y rechazo de cierres tardíos
  o con identidad de instalación distinta.
  Un caso comprueba el rechazo de una función de reloj vacía y ocho casos cubren
  los resultados de `PublishPolicy`, incluidos el cierre y el cambio de contexto
  durante la preparación del estado.
  Cinco casos comprueban `ReadState`: ausencia de estado, contexto sin política,
  contenido completo publicado, instante del reloj inyectado y conservación de
  una referencia anterior tras sustituir o cerrar el estado actual.
  Otros 16 casos cubren `ReadPolicy`, el límite exacto de 15 minutos, la renovación
  con una política idéntica, la conservación del plazo ante lecturas o respuestas
  rechazadas, los cambios durante la consulta y dos recorridos con hilos concurrentes.
- [security_context_test.cpp](security/security_context_test.cpp): cinco casos
  que comprueban la conservación de identificadores de 64 bits y la igualdad
  de contextos. Una generación, un dispositivo o una compañía distintos hacen
  que la comparación devuelva `false`; se comprueban ambos órdenes de comparación.
- [url_target_test.cpp](security/url_target_test.cpp): tres casos que comprueban
  la conservación de los datos del destino, la interpretación fallida y la
  independencia de las cadenas respecto a modificaciones en las de origen.
- [url_filter_test.cpp](security/url_filter_test.cpp): 84 casos. Incluyen 12
  pruebas directas, 12 destinos inválidos o con esquemas no admitidos, 32
  combinaciones de dominios y reglas, siete destinos clasificados como IP,
  11 prioridades entre bloqueos, siete lecturas incompletas o no utilizables
  y tres límites de caducidad. Las tablas de dominios e IP comprueban ambos
  modos, activación/desactivación y HTTP/HTTPS dentro de cada caso registrado.
  Estas combinaciones internas no aumentan el número de casos que muestra CTest.

El 9 de noviembre de 2026 se superaron las 14 pruebas
en Debug y en Release. En Debug, CTest informó de 0 fallos y 4,68 segundos.
Después de incorporar `UrlFilteringConfig`, se superaron las 17 pruebas
en ambas configuraciones. Al añadir `DownloadControlConfig`, se superaron las
20 pruebas en Debug y en Release. Estas comprobaciones acreditan
`SecurityDecision` y la conservación de datos de ambas configuraciones.
No acreditan la evaluación de dominios o archivos ni su aplicación en CEF.

El 10 de noviembre de 2026, tras incorporar `PolicySnapshot`, se superaron
las 23 pruebas en Debug y en Release. Este resultado acredita la
conservación de los datos de la política mediante el constructor y los métodos
de lectura. En ese punto seguían pendientes la validación semántica y el estado
de `PolicyManager` de la tarea #115.

Ese mismo día, tras preparar ICU e integrar su enlace y copia de archivos en
CMake, se comprobó de nuevo la compilación y se superaron las 23 pruebas
en Debug y Release. Aquella batería de 23 casos todavía no ejercitaba la
conversión Unicode de ICU ni la validación de `PolicyManager`.

Después de incorporar la validación y sus pruebas, el 10 de noviembre de 2026
se superaron las 132 pruebas en Debug y Release. Los casos
Unicode ejercitan ICU para detectar duplicados como `Ñ`/`ñ`, la expansión de
`İ` a `i` seguida de punto combinado y la sigma griega final. Comprueban que el
límite de 20 puntos de código se aplica después de convertir a minúsculas y
que se conservan las diferencias entre acentos, secuencias combinadas y otras
cadenas distintas. Las conversiones especiales se contrastaron con
[SpecialCasing de Unicode 16.0](https://www.unicode.org/Public/16.0.0/ucd/SpecialCasing.txt).

Este resultado acredita la validación de datos. En ese momento seguían pendientes
la gestión del contexto de sesión e instalación, la publicación de políticas en
un estado inmutable y la vigencia mediante reloj monotónico.
`Validate` no publica ni modifica el snapshot.
La presencia y conversión de los campos REST, incluidas las fechas, corresponden
al adaptador de adquisición todavía pendiente. Tampoco se acredita con estas
pruebas la evaluación de una navegación o descarga ni su aplicación en CEF.

El 11 de noviembre de 2026, después de incorporar `SecurityContext`, se superaron
las 137 pruebas en Debug y Release. Estos cinco casos nuevos
acreditan el objeto de contexto y su comparación.

Ese mismo día, tras incorporar `BeginContext` e `IsCurrentContext`, se superaron las
151 pruebas en Debug y Release. Los 14 casos nuevos comprueban la
creación de generaciones y la sustitución y consulta del contexto publicado.
Una identidad anterior deja de coincidir aunque se repitan dispositivo y compañía;
una creación con IDs inválidos no reemplaza el contexto ni consume generaciones.
Tras incorporar `EndContext`, se superaron también las 158 pruebas
en Debug y Release el 11 de noviembre de 2026. Los siete casos nuevos
comprueban que cerrar el contexto deja de reconocerlo como actual, que un cierre
repetido devuelve `false`, que volver a iniciar no reutiliza la generación y que
un cierre tardío o de otra instalación no termina el contexto actual.

Al incorporar el reloj inyectable y `PublishPolicy`, se superaron las 167 pruebas
en Debug y Release el 11 de noviembre de 2026. Se comprueban el rechazo del reloj
vacío, la publicación válida y los rechazos por contexto ausente, terminado o
anterior, política inválida y compañía ajena. Dos casos utilizan la función de
reloj para provocar un cierre o cambio de contexto después de validar los datos
y antes de sustituir el estado, comprobando la correspondencia final del contexto.

Tras incorporar `ReadState`, se superaron las 172 pruebas en Debug y Release el
11 de noviembre de 2026. Los cinco casos nuevos verifican los datos de la política,
su contexto y el instante local de validación como un estado coherente. Una
referencia conservada mantiene los datos e instante anteriores tras otra publicación
y permanece legible después del cierre, aunque el gestor ya no reconozca su contexto
como actual. El instante del reloj inyectado se comprueba por separado de las fechas
de auditoría del backend.

Tras incorporar `ReadPolicy` y las pruebas de vigencia y concurrencia, se superaron
las 188 pruebas en Debug y Release el 11 de noviembre de 2026. Se comprueba que la
política está disponible antes del límite y caduca al alcanzar exactamente los
15 minutos, sin esperar tiempo real. Publicar de nuevo una política válida e
idéntica renueva el plazo; leerla o rechazar una actualización inválida o de otra
sesión conserva la vigencia anterior.

Tres casos provocan cambios en el mismo hilo durante la lectura del reloj para
comprobar el contexto final y la correspondencia entre disponibilidad y estado.
Dos casos adicionales utilizan hilos reales: uno realiza 64 rondas de publicación
y lectura con referencias conservadas; otro crea 32 contextos desde cada uno de
cuatro hilos y comprueba la unicidad de las generaciones y el rechazo de cierres
anteriores. Los hilos se unen antes de las comprobaciones finales. No se ha probado
por ejecución el agotamiento del contador de generaciones de 64 bits.

La conservación de una referencia no acredita por sí sola que la política pueda
seguir autorizando operaciones. La adquisición HTTP y el tratamiento de sus errores
asociados al contexto corresponden a las tareas #122 y #123.

También se comprobaron la compilación de la aplicación en Debug y Release,
los comprobadores `check-runtime.ps1` y `check-startup.ps1` en ambas
configuraciones y el rechazo esperado de Qt ausente mediante
`check-configuration-rejection.ps1 -TimeoutSeconds 120`.
El inventario comprobado incluye las DLL y los avisos de ICU.

Durante el enlace apareció el aviso `LNK4199`: se omite la carga diferida de
`icuuc76.dll` porque la aplicación todavía no importa funciones de esa DLL.
El aviso no impide compilar; la opción queda preparada para cuando el motor
utilice la API de ICU. En ese punto quedaban pendientes la ejecución de CI
y una nueva pasada de la matriz visual completa.

La tarea #115 se integró mediante la
[PR #128](https://github.com/CristianPG1993/securebrowser-platform/pull/128).
Después del merge, finalizaron correctamente
[Desktop CI](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/43075151756)
y [Backend CI](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/43075151799)
para el commit `9a9fe1f`. Este resultado corresponde a la entrega de #115,
no a los cambios posteriores de `UrlFilter`.

El 12 de noviembre de 2026, durante la
[tarea #116](https://github.com/CristianPG1993/securebrowser-platform/issues/116),
se confirmó la compilación y se superaron las 275 pruebas en Debug y Release.
La incorporación de `UrlTarget` y `UrlHostKind` añade tres casos a los 188
anteriores; los 84 casos de `UrlFilter` completan la batería actual.

Se comprueban coincidencias exactas y de subdominio con límite de etiqueta,
nombres engañosos, listas vacías, varias reglas y coincidencias en distintas
posiciones. La normalización de copias de reglas admite mayúsculas ASCII y un
punto final sin modificar el snapshot. Los dominios internacionales se comparan
mediante los valores ASCII proporcionados, sin introducir alias IDN.
Los literales IPv4/IPv6 se bloquean con filtrado activo en ambos modos y no se
comparan por sufijos DNS; desactivar el filtrado omite esas reglas, conservando
los controles generales de política y destino.

También se comprueban el primer motivo de bloqueo, la ausencia y caducidad de
política, las lecturas incoherentes y los modos desconocidos, sin resultados
permisivos. Las actualizaciones rechazadas conservan las reglas y el plazo
anteriores; las evaluaciones repetidas no consultan el reloj ni renuevan la vigencia
ni modifican el estado. El límite de 15 minutos se prueba antes, en el instante
exacto y después mediante el reloj inyectado, sin esperas reales.

Estas pruebas reciben hosts ya interpretados, clasificados y preparados en ASCII.
No acreditan que el evaluador analice o normalice el host de una URL completa.
El parser real de CEF, la clasificación de sus variantes IP, las excepciones
internas, la compatibilidad IDN y la cancelación efectiva de navegación se
comprobarán en #117 y en la integración. `Evaluate` recibe una lectura reciente
de `ReadPolicy`; conservar su resultado no prolonga su vigencia ni sustituye la
comprobación de contexto y autorización al aplicar la decisión.
La aplicación, la adquisición REST y los eventos todavía no utilizan este
evaluador. La tarea #116 se integró en `main` mediante la
[PR #129](https://github.com/CristianPG1993/securebrowser-platform/pull/129),
commit `92c8106`, tras finalizar correctamente
[Desktop CI](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/43209501763)
y [Backend CI](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/43209501790).
Estos resultados no acreditan todavía la aplicación del filtrado en CEF.

La aplicación conserva `SBP_BUILD_DESKTOP_APP=ON` y `BUILD_TESTING=OFF` por
defecto. Las fuentes de GoogleTest, el SDK ICU y sus salidas permanecen en `.deps/` y `build/`;
no se incorporan al control de versiones.

## Pruebas del adaptador de URL con CEF

La [tarea #117](https://github.com/CristianPG1993/securebrowser-platform/issues/117)
incorpora una batería independiente con el parser real del SDK fijado.
Requiere las mismas dependencias preparadas del núcleo y el SDK CEF, incluidos
`Debug/bootstrapc.exe` y `Release/bootstrapc.exe`. No requiere Qt.
`SBP_BUILD_CEF_TESTS` está desactivada por defecto y exige `BUILD_TESTING=ON`.

Configurar una carpeta independiente:

```powershell
$cef = Get-Content desktop/cef-sdk.json -Raw -Encoding UTF8 | ConvertFrom-Json
$cefRoot = (Resolve-Path "desktop/.deps/cef/cef_binary_$($cef.version)_windows64").Path
cmake -S desktop -B desktop/build/cef-tests -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143 -DSBP_BUILD_DESKTOP_APP=OFF -DBUILD_TESTING=ON -DSBP_BUILD_CEF_TESTS=ON "-DCEF_ROOT=$cefRoot"
```

Compilar y ejecutar en Debug y Release:

```powershell
cmake --build desktop/build/cef-tests --config Debug --target sbp_cef_adapter_tests --parallel 4
ctest --test-dir desktop/build/cef-tests -C Debug -R "^sbp_cef_adapter_tests$" -V --no-tests=error

cmake --build desktop/build/cef-tests --config Release --target sbp_cef_adapter_tests --parallel 4
ctest --test-dir desktop/build/cef-tests -C Release -R "^sbp_cef_adapter_tests$" -V --no-tests=error
```

CTest registra una batería llamada `sbp_cef_adapter_tests`, que ejecuta los
24 casos parametrizados de [cef_url_adapter_test.cpp](browser/cef_url_adapter_test.cpp),
dos casos de [security_request_handler_test.cpp](browser/security_request_handler_test.cpp)
y tres casos de [browser_client_test.cpp](browser/browser_client_test.cpp),
además de diez casos de [security_navigation_test.cpp](browser/security_navigation_test.cpp)
en una sola sesión de CEF. El resumen de CTest muestra una prueba; GoogleTest
informa de los 39 casos. El XML se conserva en
`desktop/build/cef-tests/cef-adapter/<configuración>/cef-adapter-results.xml`.
Los 275 casos del núcleo mantienen sus comandos y carpeta independientes.

El bootstrap de consola carga la DLL de pruebas y llama a
[cef_adapter_test_entry_win.cpp](browser/cef_adapter_test_entry_win.cpp).
Se inicializa CEF con sandbox, sin crear QApplication. Los casos del adaptador
y de propiedad de referencias no crean un navegador. Los casos de navegación
crean un navegador nativo oculto por caso y esperan su cierre antes de continuar.
El lanzador [run_cef_adapter_tests.cmake](browser/run_cef_adapter_tests.cmake)
crea un perfil exclusivo bajo `cef-adapter/<configuración>/test-profiles/` y lo
entrega mediante la variable de entorno `SBP_CEF_ADAPTER_TEST_PROFILE`.
Espera a la salida completa del proceso antes de limpiar, comprueba el destino
y reintenta brevemente si Windows mantiene algún archivo bloqueado.
Los fallos conservan el perfil y sus diagnósticos; una batería sin casos también
se considera un error. El lanzador limita la ejecución a 90 segundos y CTest
aplica un límite exterior de 120 segundos.

El 12 de noviembre de 2026 se superaron los 24 casos en Debug y Release.
Se comprueban esquema y host, mayúsculas, punto final, puerto, userinfo,
host codificado, dominio internacional, variantes IPv4 abreviadas, decimal,
hexadecimal y octal, IPv6, esquemas no web y entradas inválidas.
Se comprueba la interpretación de `about:blank`, `data:` y `file:` sin
atribuir a la interpretación correcta una autorización de navegación.

En la primera ejecución Debug pasaron los 24 casos, pero falló la limpieza
intentada dentro del proceso de CEF. Tras trasladarla al lanzador exterior,
se confirmó la salida correcta de CTest y la retirada del perfil; la ejecución
Debug registrada completó la batería en 2,18 segundos.

Después de incorporar el constructor de `SecurityRequestHandler` y sus pruebas,
se superaron los 26 casos en Debug y Release el 12 de noviembre de 2026.
Se comprueba el rechazo de un gestor nulo y que el último `CefRefPtr` libera
el handler y su propiedad compartida del gestor, sin duplicar el objeto.

El 13 de noviembre de 2026 se completó la validación de los 29 casos en Debug
y Release. Los tres casos de `BrowserClient` comprueban el rechazo de un handler
vacío, la devolución del mismo objeto inyectado mediante `GetRequestHandler`
y su conservación mientras lo retengan el cliente o una referencia de CEF.
Los XML de ambas configuraciones registran 29 casos sin fallos ni errores.

La evidencia de los 29 casos acredita `CefUrlAdapter::Parse`, su clasificación mediante el
parser fijado, la construcción y vida del handler y su entrega desde `BrowserClient`.
En ese punto no se había acreditado `OnBeforeBrowse`, redirecciones, subframes,
historial, caché, service workers ni cancelación efectiva de páginas.

La batería incorpora diez casos de navegación principal con un cliente de prueba.
Los ocho casos web exigen recibir el callback real de CEF y delegan en
`SecurityRequestHandler`. Para los dos de `about:blank`, la carga real y el
permiso del handler se comprueban por separado: tras cargar el documento,
se llama directamente al handler en el hilo UI, con navegador y frame reales.
`callbackObserved` conserva el valor `false`; `handlerCheckedDirectly` permite
distinguir esta comprobación de un callback emitido por CEF.

El [interceptor de CEF fijado](https://github.com/chromiumembedded/cef/blob/4fa4ab516dacf6c49fc7e6e70d2b7ebce29fa35f/libcef/browser/net/throttle_handler.cc)
utiliza las fases de petición y redirección de Chromium. La
[API de navegación de Chromium 156](https://github.com/chromium/chromium/blob/156.0.8078.48/content/public/browser/navigation_throttle.h)
documenta que navegaciones como `about:blank` pueden omitir esos interceptores.
No se atribuye a los dos casos internos una intervención previa del handler
en la carga real ni se modifica la política de la aplicación.
Comprueban `about:blank` sin contexto y con política caducada, HTTP y HTTPS
sin contexto, contexto sin política, política caducada, contexto cerrado,
dominio denegado, allowlist vacía e IP literal. Los casos internos permitidos
deben completar la carga; los bloqueados se cierran tras observar la decisión.
Si el handler concede inesperadamente una URL web, el cliente de prueba
registra ese resultado para que falle la aserción y cancela la petición antes
de salir a la red. No se acredita con esta batería una carga HTTP permitida
ni la recepción de peticiones en un servidor de destino.

En las dos primeras ejecuciones de los 39 casos en Debug, el 13 de noviembre
de 2026, se superaron 37 y fallaron los dos de `about:blank`: el documento se
cargaba y el navegador se cerraba, pero no se observaba `OnBeforeBrowse`.
La segunda ejecución partía de un documento local previo; esa preparación
tampoco hacía que CEF emitiera el callback para `about:blank` y se ha retirado.
Tras contrastar el comportamiento con las fuentes fijadas, se han corregido
las expectativas y añadido la comprobación directa del permiso interno.
Después de esta corrección, el 13 de noviembre de 2026 se superaron los 39 casos
en Debug y Release. Los XML de ambas configuraciones registran cero fallos y
cero errores; CTest confirmó la eliminación del perfil aislado tras la salida
del proceso. La ejecución Release registrada completó CTest en 3,69 segundos.
Este resultado acredita los ocho callbacks web y las dos comprobaciones internas
con el alcance descrito. Siguen pendientes las cargas HTTP permitidas con
servidor controlado, las redirecciones, los subframes y los escenarios de
historial, caché y service workers de #117.

## Comprobaciones automáticas de Desktop Foundation

Preparar las salidas `desktop/build/Debug` y `desktop/build/Release`. Cerrar
las instancias de SecureBrowser antes de iniciar la batería: ambas
configuraciones comparten el perfil CEF y deben ejecutarse una detrás de otra.

| Comprobador | Qué acredita | Resultado esperado |
| --- | --- | --- |
| [check-runtime.ps1](check-runtime.ps1) | Presencia de los archivos del inventario generado por CMake. Admite `-RuntimeDirectory` para comprobar una copia aislada. | `0` si están presentes; `1` si falta alguno o el inventario es inválido. |
| [check-startup.ps1](check-startup.ps1) | Aparición de la ventana hija nativa de CEF, cierre normal, salida `0` y marcas de creación y apagado en orden para el mismo navegador. | `0` si completa el ciclo; `1` si falla o supera un plazo. |
| [check-configuration-rejection.ps1](check-configuration-rejection.ps1) | Rechazo de una configuración nueva con `Qt6_DIR` vacío y el diagnóstico específico de Qt. | `0` si CMake rechaza ese caso; `1` si lo acepta, falla por otro motivo o supera el plazo. |

La batería normal utiliza los SDK ya instalados:

```powershell
.\desktop\tests\check-configuration-rejection.ps1
if ($LASTEXITCODE -ne 0) { throw 'Ha fallado la comprobación de configuración.' }

foreach ($configuration in @('Debug', 'Release')) {
    .\desktop\tests\check-runtime.ps1 -InventoryPath "desktop/build/runtime-inventory-$configuration.json"
    if ($LASTEXITCODE -ne 0) { throw "Runtime $configuration incompleto." }

    .\desktop\tests\check-startup.ps1 -Configuration $configuration
    if ($LASTEXITCODE -ne 0) { throw "Ha fallado el arranque o cierre $configuration." }
}
```

`check-startup.ps1` abre una ventana y solicita su cierre automáticamente.
El plazo predeterminado es de 30 segundos para detectar la ventana CEF y
otros 30 para finalizar. Se puede ajustar mediante `-StartupTimeoutSeconds`
y `-CloseTimeoutSeconds`. El log se lee después de terminar el proceso,
porque la aplicación lo mantiene abierto en modo exclusivo mientras escribe.
El script muestra la ruta del diagnóstico comprobado.

La configuración negativa tiene un plazo predeterminado de 60 segundos,
ajustable con `-TimeoutSeconds`. Utiliza una carpeta única bajo
`desktop/build/configuration-rejection-...` y comprueba su ruta antes de
borrarla. Tras cancelar el intento, reintenta el borrado durante cinco
segundos si quedan bloqueos temporales y comunica cualquier limpieza incompleta.

Los dos scripts que lanzan procesos conservan el handle del proceso creado.
Cuando necesitan interrumpirlo, invocan `taskkill /PID <su PID> /T /F` sobre
ese intento y esperan hasta cinco segundos a su finalización. La limpieza
no selecciona procesos por nombre. `check-startup.ps1` rechaza la prueba si
ya hay una instancia de SecureBrowser abierta.

## Casos negativos preparados

### Runtime incompleto

El inventario original permite verificar una copia con una DLL ausente.
Para preparar el caso sin alterar la salida normal, copiar únicamente los
archivos enumerados y retirar `SecureBrowser.dll` de esa copia:

```powershell
$inventoryPath = 'desktop/build/runtime-inventory-Debug.json'
$inventory = Get-Content -LiteralPath $inventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json
$copyName = 'runtime-missing-' + [Guid]::NewGuid().ToString('N')
$runtimeCopy = Join-Path (Resolve-Path -LiteralPath 'desktop/build').Path $copyName

foreach ($relativeFile in $inventory.required_files) {
    $destination = Join-Path $runtimeCopy $relativeFile
    $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
    Copy-Item -LiteralPath (Join-Path $inventory.runtime_directory $relativeFile) -Destination $destination
}
Remove-Item -LiteralPath (Join-Path $runtimeCopy 'SecureBrowser.dll')
.\desktop\tests\check-runtime.ps1 -InventoryPath $inventoryPath -RuntimeDirectory $runtimeCopy
$LASTEXITCODE
Write-Host "Copia conservada para revisión: $runtimeCopy"
```

Esperar un mensaje que incluya `Faltan: SecureBrowser.dll` y código `1`.
Esta comprobación solo inspecciona archivos; la copia se conserva dentro de
`build`. También se puede reutilizar una copia `ClientDll` preparada por
la [comprobación manual de fallos nativos](manual/check-startup-failure.ps1).

### Timeout y limpieza

Un límite muy corto permite interrumpir la configuración mientras identifica
el compilador:

```powershell
.\desktop\tests\check-configuration-rejection.ps1 -TimeoutSeconds 1
$LASTEXITCODE
```

Para acreditar este caso deben aparecer el aviso de que se ha superado el
plazo, `Carpeta aislada de prueba eliminada.` y código `1`. Si CMake llega
antes al rechazo de Qt y el script devuelve `0`, no se ha ejercitado el timeout.
El plazo necesario depende del equipo; este caso se comprueba aparte de la
batería normal. Un mensaje de limpieza incompleta deja pendiente esa validación.

## Resultados registrados para #93

Se han ejecutado y comprobado los siguientes casos:

| Caso | Resultado confirmado |
| --- | --- |
| Runtime completo en Debug y Release | Código `0` en ambas configuraciones. |
| Copia aislada de Debug sin `SecureBrowser.dll` | Ausencia detectada y código `1`. |
| Arranque y cierre automáticos en Debug y Release | Ciclo completado y código `0` en ambas configuraciones. |
| Configuración nueva sin `Qt6_DIR` | CMake devuelve `1`; el comprobador reconoce el diagnóstico y devuelve `0`. |
| Configuración con límite de un segundo | Timeout detectado, carpeta aislada eliminada y código `1`, tras corregir el borrado inmediato de archivos aún bloqueados. |

## Matriz visual manual

Se han comprobado todas las filas en Debug y Release para #93.
Los resultados de esta tabla corresponden a esa comprobación manual.

Completar la misma secuencia en Debug y Release, con una instancia cada vez.
Para abrir una configuración e interactuar sin cierre automático:

```powershell
$configuration = 'Debug' # Repetir con Release.
$executable = (Resolve-Path -LiteralPath "desktop/build/$configuration/SecureBrowser.exe").Path
Start-Process -FilePath $executable
```

| Área | Acción y resultado esperado | Debug | Release |
| --- | --- | --- | --- |
| Renderizado | Abrir `https://example.org/`. El contenido aparece dentro de la ventana Qt, sin una ventana CEF independiente. | Correcto | Correcto |
| Navegación | Introducir otra dirección con Enter, seguir un enlace, usar Atrás, Adelante y Recargar. La barra refleja la navegación y los botones responden según el historial. | Correcto | Correcto |
| Foco | Editar la dirección, pasar al contenido y regresar; cambiar de aplicación con Alt+Tab y volver. El teclado funciona en el control seleccionado y el caret corresponde al foco. | Correcto | Correcto |
| Tamaño | Reducir, ampliar, maximizar, minimizar y restaurar la ventana. La hija CEF sigue el área disponible. | Correcto | Correcto |
| Cierre | Cerrar con la X. El diagnóstico termina con `Qt window destroyed; event loop exit code 0.` y `CEF shutdown completed.`; no quedan procesos de la prueba tras cinco segundos. | Correcto | Correcto |

Las pruebas adicionales de confirmación `beforeunload`, bloqueo de popups y
cierre durante una carga lenta están descritas en el
[README de Desktop](../README.md#confirmación-de-abandono-y-pruebas-manuales-de-cierre).
Sus resultados anteriores se conservan con sus limitaciones en las
[notas de implementación](../docs/implementation-notes.md#confirmación-de-abandono-y-pruebas-manuales-de-cierre).

## Límites de cobertura

El inventario comprueba presencia, no integridad, arquitectura ni capacidad
de carga de las DLL. Enumera los binarios y recursos de CEF y las dependencias
Qt esenciales; no representa todas las dependencias opcionales que puede
copiar `windeployqt` ni los requisitos de instalación del runtime de MSVC.

La ventana hija y los logs acreditan creación y finalización del navegador.
El script no espera una carga de red ni acredita renderizado, navegación,
foco, tamaño, diálogos de abandono o ausencia de auxiliares tras una salida
normal. Esas comprobaciones corresponden a la matriz manual. El timeout
forzado registrado corresponde a CMake; no simula un bloqueo del hilo UI de CEF.

Las comprobaciones de Desktop Foundation de #93 utilizan PowerShell y CMake.
Su validación se centra en la integración de herramientas, archivos y procesos.
Las pruebas unitarias con GoogleTest del Security Engine se describen al inicio
de esta guía y tienen un alcance independiente.
