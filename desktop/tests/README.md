# Verificación de Desktop

Esta guía recoge la verificación de Desktop Foundation de la
[tarea #93](https://github.com/CristianPG1993/securebrowser-platform/issues/93)
y las pruebas unitarias del Security Engine iniciadas en la
[tarea #115](https://github.com/CristianPG1993/securebrowser-platform/issues/115).
Ejecutar los comandos desde la raíz del repositorio, con Windows PowerShell 5.1
o PowerShell en Windows. La preparación de MSVC, Qt y CEF y los comandos de
construcción están en el [README de Desktop](../README.md).

## Pruebas unitarias del Security Engine

Estas pruebas requieren CMake 3.29 o posterior, MSVC 2022 x64 con el toolset
v143 y el SDK de Windows 10.0.28000.0 utilizado por los comandos siguientes.
El núcleo y las pruebas se configuran sin buscar Qt ni CEF.

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
ctest --test-dir desktop/build/tests -C Debug --output-on-failure --no-tests=error
```

Repetir en Release utilizando la misma carpeta del generador de Visual Studio:

```powershell
cmake --build desktop/build/tests --config Release --target sbp_security_tests --parallel 4
ctest --test-dir desktop/build/tests -C Release --output-on-failure --no-tests=error
```

CTest descubre cada prueba de GoogleTest antes de ejecutarla, incluidos los
casos parametrizados. Actualmente se esperan 23 casos:

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

El 9 de noviembre de 2026, el usuario confirmó las 14 pruebas superadas
en Debug y en Release. En Debug, CTest informó de 0 fallos y 4,68 segundos.
Después de incorporar `UrlFilteringConfig`, confirmó las 17 pruebas superadas
en ambas configuraciones. Al añadir `DownloadControlConfig`, confirmó las
20 pruebas superadas en Debug y en Release. Estas comprobaciones acreditan
`SecurityDecision` y la conservación de datos de ambas configuraciones;
no acreditan la evaluación de dominios o archivos ni su aplicación en CEF.

El 10 de noviembre de 2026, tras incorporar `PolicySnapshot`, el usuario
confirmó las 23 pruebas superadas en Debug y en Release. Esto acredita la
conservación de los datos de la política mediante el constructor y los métodos
de lectura. La validación semántica y la implementación de `PolicyManager`
de la tarea #115 siguen pendientes, incluidos contexto, publicación y vigencia.

La aplicación conserva `SBP_BUILD_DESKTOP_APP=ON` y `BUILD_TESTING=OFF` por
defecto. Las fuentes y salidas de GoogleTest permanecen en `.deps/` y `build/`;
no se incorporan al control de versiones.

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

El usuario ha ejecutado y confirmado los siguientes casos:

| Caso | Resultado confirmado |
| --- | --- |
| Runtime completo en Debug y Release | Código `0` en ambas configuraciones. |
| Copia aislada de Debug sin `SecureBrowser.dll` | Ausencia detectada y código `1`. |
| Arranque y cierre automáticos en Debug y Release | Ciclo completado y código `0` en ambas configuraciones. |
| Configuración nueva sin `Qt6_DIR` | CMake devuelve `1`; el comprobador reconoce el diagnóstico y devuelve `0`. |
| Configuración con límite de un segundo | Timeout detectado, carpeta aislada eliminada y código `1`, tras corregir el borrado inmediato de archivos aún bloqueados. |

## Matriz visual manual

El usuario ha confirmado todas las filas en Debug y Release para #93.
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
