# ADR-0002: Selección de la interfaz desktop

- Estado: Propuesta
- Fecha: 2026-10-02

## Contexto

La aplicación Desktop de SBP utilizará C++ y Chromium Embedded Framework
(CEF).

La tecnología de interfaz está pendiente de decidir. Se compararán
Qt 6 Widgets y CEF Views mediante un prototipo técnico.

## Opciones consideradas

### Qt 6 Widgets

Utilizar Qt 6 Widgets para construir la interfaz e integrar el navegador CEF.

La evaluación deberá comprobar la integración entre ambas tecnologías
y las dependencias necesarias para compilar y distribuir la aplicación.

### CEF Views

Utilizar el framework Views de CEF para construir la interfaz
y alojar el navegador mediante sus componentes.

La evaluación deberá comprobar si sus controles y posibilidades
de personalización cubren las necesidades de SBP.

## Decisión

La elección permanece pendiente.

Se decidirá después de comparar ambas alternativas mediante un prototipo
técnico y documentar sus resultados.

Los criterios de evaluación siguientes son una propuesta inicial.

## Consecuencias

- La interfaz definitiva depende del resultado de la comparación.
- El prototipo permitirá identificar dificultades de integración.
- La evaluación incluirá las dependencias y sus condiciones de licencia.

## Validación

Se propone comparar ambas alternativas en Windows con un alcance equivalente:

- Mostrar una ventana con un navegador CEF.
- Cargar una URL introducida por el usuario.
- Probar navegación básica, redimensionamiento y foco del teclado.
- Comprobar el cierre de la ventana y de los procesos asociados.
- Evaluar la compilación y distribución con CMake.
- Registrar el esfuerzo de integración y las limitaciones encontradas.
- Revisar las condiciones de licencia de las dependencias utilizadas.

Se documentarán las versiones, las condiciones de las pruebas
y los resultados antes de aceptar una alternativa.

## Referencias

- [Arquitectura general](../architecture/overview.md).
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html).
- [CEF Views: CefBrowserView](https://github.com/chromiumembedded/cef/blob/master/include/views/cef_browser_view.h).