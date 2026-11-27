# Distribución autónoma de navegación y descargas

La entrega portátil de [#131](https://github.com/CristianPG1993/securebrowser-platform/issues/131),
ampliada con Download Control en [#119](https://github.com/CristianPG1993/securebrowser-platform/issues/119),
utiliza el ejecutable Release y el motor de seguridad existente. El lanzador
activa expresamente `--demo-url-filtering --demo-policy=free`; la selección
posterior se realiza desde la ventana. El modo normal conserva el bloqueo
por defecto mientras no disponga de una política válida.

## Instalador para la tutora

Desde la raíz del repositorio, con Inno Setup 6.3 o posterior de la serie 6 en
el equipo de desarrollo, ejecuta estos comandos en PowerShell:

```powershell
cmake --build desktop/build --config Release --target SecureBrowser --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Falló la compilación Release.' }
.\desktop\distribution\build-installer.ps1
```

El script ejecuta el empaquetador portátil, comprueba todos los tamaños y
SHA-256 del manifiesto y entrega `SecureBrowser-Setup.exe` y su
`.sha256` en una nueva carpeta `*-setup` bajo `desktop/build/packages/`.
Si ISCC está en una ubicación personalizada, indica `-IsccPath`.
La [guía de la demo](LEEME-demo.txt) también se instala en el menú Inicio.

El asistente instala para el usuario actual en
`%LOCALAPPDATA%\Programs\SecureBrowser Demo`. El acceso directo usa
`Iniciar-demo.cmd` con esa carpeta de trabajo. Antes de abrir el navegador,
el lanzador aplica el mismo permiso heredable de lectura y ejecución del
SDK para el SID LPAC `S-1-15-2-2`. Si falla, el arranque se detiene.
La desinstalación de Windows retira los archivos del instalador y sus accesos
directos. El perfil de CEF, los diagnósticos y las descargas quedan en la
cuenta del usuario. El instalador académico se distribuye sin firma digital.

## Preparación del ZIP portátil desde el repositorio

La compilación requiere los SDK y las herramientas del
[README de Desktop](../README.md). El empaquetado requiere una salida Release
ya preparada por CMake y `windeployqt`, y los redistribuibles x64 de VS 2022.
Desde la raíz del repositorio:

```powershell
cmake --build desktop/build --config Release --target SecureBrowser --parallel 1
if ($LASTEXITCODE -ne 0) { throw 'Compilación Release fallida.' }
.\desktop\distribution\package-demo.ps1
```

El script no compila ni inicia la aplicación. Crea una carpeta con nombre
único bajo `desktop/build/packages/`, su ZIP y un archivo `.zip.sha256`.
No elimina entregas anteriores. Una carpeta parcial tras un fallo no se
considera un paquete válido.

Parámetros opcionales:

| Parámetro | Uso |
| --- | --- |
| `-InventoryPath` | Inventario Release generado por CMake; por defecto, `desktop/build/runtime-inventory-Release.json`. |
| `-OutputDirectory` | Carpeta que contendrá las entregas nuevas. |
| `-CompilerRuntimeDirectory` | Carpeta `x64/Microsoft.VC143.CRT` de los redistribuibles; si se omite, se localiza mediante `vswhere` en VS 2022. |

## Contenido

El paquete conserva los archivos del inventario, las DLL y carpetas Qt
desplegadas, los recursos CEF, los avisos Qt/CEF/ICU y las DLL redistribuibles
de Visual C++. Excluye bibliotecas de desarrollo, herramientas de comprobación
y logs de la salida de compilación. Añade:

- `Iniciar-demo.cmd`, para abrir la demo por doble clic.
- `LEEME-demo.txt`, guía de una página destinada a la evaluación manual.
- `package-manifest.json`, con las rutas relativas, tamaños y SHA-256 de los
  archivos preparados. El manifiesto no incluye su propio hash.

El ZIP no conserva las ACL de NTFS. El lanzador prepara en su carpeta el mismo
permiso LPAC de lectura y ejecución que aplica `SET_LPAC_ACLS` en el SDK CEF
fijado. No desactiva el sandbox ni añade excepciones para destinos de prueba.
Si ese permiso no se puede preparar, muestra un error y termina.

## Comprobación antes de entregar

1. Comprobar el hash del ZIP frente al archivo `.zip.sha256` y verificar los
   archivos contra el manifiesto.
2. Extraer el ZIP completo en una carpeta local del usuario fuera de `build`.
3. Cerrar otras instancias y abrir el lanzador desde la copia extraída.
4. Seguir `LEEME-demo.txt`: destino permitido, retirada del documento al cambiar
   la política, bloqueo de Atrás/Adelante, destino permitido bajo Restrictiva y
   vuelta a Libre.
5. Seguir la [guía de descargas controladas](../tests/manual/download-control-demo/README.md):
   guardado permitido, nombre sugerido y definitivo bloqueados, cancelación
   voluntaria y avisos de resultado. Cerrar mediante la X.
6. Conservar los informes automatizados y los logs de
   `%LOCALAPPDATA%\SecureBrowser\Logs` junto con el identificador del paquete.

La [validación de navegación](../docs/url-filtering-demo-validation.md) identifica
el ZIP anterior y sus comprobaciones. La [validación de descargas](../tests/manual/download-control-demo/README.md#paquete-portátil-comprobado)
registra el ZIP del 19 de noviembre, sus hashes y el alcance de esa pasada Windows. La
[prueba HTTP controlada](../tests/manual/url-filtering-demo/README.md) y la
regresión automatizada distinguen una cancelación real de un fallo de red.

El paquete no necesita backend ni Python para ejecutarse. Incluye Download
Control con las políticas locales Libre y Restrictiva. El lanzador selecciona
siempre ese modo autónomo. El servidor Python de las pruebas manuales se utiliza
solo en desarrollo y no se distribuye con la aplicación. Las identidades
son de demostración y sus políticas no caducan; el modo normal mantiene la
vigencia de 15 minutos y no utiliza estas políticas como fallback.
