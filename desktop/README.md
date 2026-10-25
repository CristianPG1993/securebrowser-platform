# SecureBrowser Desktop

## Estado actual

La tarea #79 incorpora `sbp_toolchain_check`, un ejecutable temporal que
comprueba la cadena de herramientas C++. Su entrada `main` devuelve `0`,
sin mostrar texto ni abrir ventanas. Qt y CEF (Chromium Embedded Framework)
se integrarán en tareas posteriores.

## Requisitos

- Windows 11 x64.
- CMake 3.29 o superior; versión de referencia: 3.29.2.
- Visual Studio Build Tools 2022 con las herramientas C++ MSVC
  (Microsoft Visual C++) v143 para x64.
- Windows SDK (Software Development Kit, kit de desarrollo de software),
  paquete `10.0.28000.2270`. El identificador de instalación utilizado por
  CMake es `10.0.28000.0`.

Las decisiones de integración están recogidas en el
[ADR-0011 (Architecture Decision Record, registro de decisión arquitectónica)](../docs/adr/0011-desktop-cef-integration.md).

## Configurar

Ejecutar en PowerShell desde la raíz del repositorio:

```powershell
cmake -S desktop -B desktop/build -G "Visual Studio 17 2022" -A "x64,version=10.0.28000.0" -T v143
```

`-S` selecciona el directorio de fuentes y `-B` el de construcción.
`-G` fija el generador Visual Studio 2022; `-A` selecciona x64 y el Windows
SDK; `-T` selecciona el conjunto de herramientas v143.

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
