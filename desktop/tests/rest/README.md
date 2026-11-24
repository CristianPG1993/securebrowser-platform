# Pruebas REST y del lector de tokens — #122

Suite de 17 pruebas GoogleTest: 11 del cliente `RestClient`, usando un servidor
TCP/HTTP falso dentro del mismo proceso, y 6 de `TokenResponseParser`, sin red.
El servidor escucha únicamente en `127.0.0.1`, en un puerto libre asignado por
Windows. No utiliza Spring Boot, PostgreSQL, CEF, credenciales reales ni variables
de `.env`. Los datos de autenticación de los casos son fixtures.

## Qué comprueba

- GET asíncrono con Bearer explícito, estado/cuerpo conservados y una sola entrega.
- POST con cuerpo JSON, sin añadir Authorization implícitamente al login.
- HTTP 401 y 503 como respuestas del servidor, separadas de los fallos de transporte.
- Desconexión sin respuesta y HTTP 200 con cuerpo truncado.
- Timeout con el plazo real de producción de 15 segundos; es el caso más lento.
- Cancelación de dos peticiones pendientes, una entrega por petición.
- Destrucción del cliente durante una petición y desde su propia callback.
- Rechazo de peticiones inválidas antes de enviar datos.
- Redirección devuelta sin seguirla ni reenviar Authorization al destino.
- Lectura completa del DTO de tokens, tipos obligatorios, contenido y duración entera de 64 bits.

No acredita login/refresh reales, TLS contra un servidor remoto, identificación del
Device, política aplicada ni E2E con CEF. Esas verificaciones corresponden a los
siguientes incrementos. Los resultados comprobados se recogen en la evidencia
local; las configuraciones restantes y CI conservan su validación pendiente.

## Preparación y ejecución

Desde la raíz del repositorio, con el toolchain MSVC x64 habitual y las copias
preparadas de ICU y GoogleTest descritas en [la guía general](../README.md).
El Qt fijado es 6.12.0. Ajustar `Qt6_DIR` si el kit está en otra carpeta.

```powershell
cmake -S desktop -B desktop/build/rest-tests -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143 -DSBP_BUILD_DESKTOP_APP=OFF -DBUILD_TESTING=ON -DSBP_BUILD_REST_TESTS=ON "-DQt6_DIR=C:/Qt/6.12.0/msvc2022_64/lib/cmake/Qt6"
```

Compilar y ejecutar Debug:

```powershell
cmake --build desktop/build/rest-tests --config Debug --target sbp_rest_tests --parallel 1
$restReportDirectory = (Resolve-Path 'desktop/build/rest-tests').Path
ctest --test-dir desktop/build/rest-tests -C Debug -R '^REST\.' --output-on-failure --no-tests=error --output-junit "$restReportDirectory/rest-Debug.xml"
```

Repetir en Release:

```powershell
cmake --build desktop/build/rest-tests --config Release --target sbp_rest_tests --parallel 1
$restReportDirectory = (Resolve-Path 'desktop/build/rest-tests').Path
ctest --test-dir desktop/build/rest-tests -C Release -R '^REST\.' --output-on-failure --no-tests=error --output-junit "$restReportDirectory/rest-Release.xml"
```

Se esperan 17 casos por configuración, sin fallos. Cada caso tiene un límite CTest
de 30 segundos; el del timeout deja hasta 20 segundos al bucle de eventos para
observar el vencimiento de 15 segundos. La espera procesa eventos Qt, sin bloquear
con sleeps el hilo que atiende cliente y servidor. El ejecutable copia Core y
Network junto a su salida; no requiere modificar el PATH ni plugins de ventanas.

Los informes XML quedan en `desktop/build/rest-tests/`. Ante un fallo, conservar
también `Testing/Temporary/LastTest.log` y el SHA de la revisión probada. Registrar
comando, configuración y resultado real antes de proponer el commit de pruebas.

## Comprobar solo el nuevo lector de tokens

Tras configurar y compilar `sbp_rest_tests`, estos comandos seleccionan las seis
pruebas nuevas. Los informes tienen nombres distintos para conservar los del
transporte. Cada grupo comprueba un comportamiento: respuesta completa, Bearer y
64 bits, JSON inválido, campos obligatorios/tipos, valores de texto y duración.

```powershell
$restReportDirectory = (Resolve-Path 'desktop/build/rest-tests').Path
ctest --test-dir desktop/build/rest-tests -C Debug -R '^REST\.TokenResponseParserTest\.' --output-on-failure --no-tests=error --output-junit "$restReportDirectory/token-parser-Debug.xml"
```

Repetir tras compilar Release:

```powershell
$restReportDirectory = (Resolve-Path 'desktop/build/rest-tests').Path
ctest --test-dir desktop/build/rest-tests -C Release -R '^REST\.TokenResponseParserTest\.' --output-on-failure --no-tests=error --output-junit "$restReportDirectory/token-parser-Release.xml"
```

Evidencia del lector revisada el 24 de noviembre de 2026:

| Configuración | Resultado | Informe local |
| --- | --- | --- |
| Debug | 6 pruebas, 0 fallos, 0 errores y 0 omitidas. | `desktop/build/rest-tests/token-parser-Debug.xml` |
| Release | 6 pruebas, 0 fallos, 0 errores y 0 omitidas. | `desktop/build/rest-tests/token-parser-Release.xml` |

Ambas compilaciones son posteriores a las fuentes del lector y sus pruebas.
La revisión de este incremento se registra en la PR #157. Desktop CI incluirá
automáticamente estos casos al ejecutar el filtro global `^REST\.`; los resultados
de una revisión anterior no validan los nuevos casos.

Huellas SHA-256 de los informes del lector revisados:

- Debug: `633102C6BEAF0F3603FB4A878C81512786896F62CE5C2B8D7D392CAC756E6EC0`.
- Release: `B97D4D4B75FBA17F17D200F8AD5D3D99BEED0250AC268EFE34C787D23E631647`.

## Regresión del navegador

Después de estos cambios de CMake/runtime, volver a compilar el navegador en Debug
y Release mediante sus comandos habituales y conservar la comprobación de arranque
y cierre. Desktop CI ejecutará la suite REST, las suites existentes y los controles
de runtime/arranque. Hasta publicar una PR y terminar sus checks, CI queda pendiente.

## Evidencia local — 23 de noviembre de 2026

El autor confirmó la ejecución y se revisaron los informes JUnit de las 11 pruebas
de transporte. Esta evidencia es anterior a añadir las seis del lector de tokens:

| Configuración | Resultado | Informe local |
| --- | --- | --- |
| Debug | 11 pruebas, 0 fallos, 0 errores y 0 omitidas; 16 segundos en total. | `desktop/build/rest-tests/rest-Debug.xml` |
| Release | 11 pruebas, 0 fallos, 0 errores y 0 omitidas; 15 segundos en total. | `desktop/build/rest-tests/rest-Release.xml` |

El código del transporte probado corresponde al commit `b869fe7`; esta entrega
incorpora la suite y su configuración de CMake y CI. Los SHA de la revisión completa
se registran en su PR. Las compilaciones de la suite son posteriores a las fuentes
de sus ejecuciones. Después se ampliaron únicamente los comentarios didácticos:
se comprobó que los seis archivos C++ conservan el mismo contenido al excluir
comentarios y espacios. Antes del commit también se compararon los dos archivos
del transporte con `b869fe7`, con el mismo resultado.
Los binarios y los informes permanecen en `build/` y no se versionan.

Huellas SHA-256 de los informes revisados:

- Debug: `8CCB2B14CF42CB95F26979BD6137B780522DD304E3702EA17B48D261DB99EFBA`.
- Release: `FA3BB0EE6C52B014EBDB7D0F2EF49CC6A074EDFABD2ADE40CA0503CB189156CE`.

La regresión del navegador y GitHub Actions de este incremento siguen pendientes.

## Ajuste de la comprobación temporal en CI

El [Desktop CI de la revisión 799f085](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/45205082778)
falló únicamente en el límite inferior medido por el test del timeout: registró
14.996 ms frente a los 15.000 ms exigidos. Las otras diez pruebas de transporte
pasaron. El resto del workflow quedó sin ejecutar tras ese fallo; Backend CI pasó.

La comprobación de duración admite ahora 100 ms de margen de medición, manteniendo
el timeout de producción en 15 segundos. Sigue comprobando `Timeout`, ausencia de
estado HTTP, recepción de la petición en el servidor y una única entrega. El
watchdog del test continúa en 20 segundos. Este ajuste del test queda pendiente
de revalidación en CI y no modifica los controles del cliente.
