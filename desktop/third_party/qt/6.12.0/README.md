# Avisos comunes de la instalación Qt 6.12.0

La carpeta [`licenses/`](licenses/) conserva copias sin modificar de los cuatro
textos presentes en `C:/Qt/Licenses` de la instalación local utilizada para
validar Desktop con Qt 6.12.0, kit `msvc2022_64`. Se incorporaron el 6 de noviembre
de 2026 y se comprobó que sus SHA-256 coincidían con los archivos de origen.

| Archivo | Contenido |
| --- | --- |
| `LICENSE` | Información general de Qt y textos GNU GPL y LGPL. |
| `LICENSE.FDL` | GNU Free Documentation License. |
| `COPYING.txt` | Texto Apache incluido en la instalación. |
| `Copyright.txt` | Avisos de CMake y sus componentes incluidos en la instalación. |

`desktop/ci/configure.ps1` entrega esta carpeta a CMake mediante
`SBP_QT_LICENSE_DIR`. Esto permite conservar los mismos avisos en CI cuando el
kit se prepara con aqt y no existe la carpeta común del instalador gráfico.
El argumento `-QtLicenseDirectory` permite seleccionar otra carpeta de avisos.

Los inventarios SPDX de `qtbase`, `qtsvg` y `qttranslations` se obtienen del
directorio `sbom/` del propio kit descargado. CMake copia esos inventarios y los
textos comunes a `qt-notices/` en la salida de ejecución.

Al cambiar de versión de Qt, revisar estos textos y añadir los correspondientes
a la nueva versión; la ruta seleccionada por CI incluye la versión del
manifiesto. Estos archivos conservan sus condiciones originales.

Referencias: [licencias de Qt 6.12](https://doc.qt.io/qt-6.12/licensing.html) e
[inventarios SBOM de Qt 6.12](https://doc.qt.io/qt-6.12/sbom.html).
