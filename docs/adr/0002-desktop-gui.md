# ADR-0002: Selección de la interfaz desktop

- Estado: Aceptada
- Fecha: 2026-10-02
- Fecha de aceptación: 2026-10-24

## Contexto

La aplicación Desktop de SBP utilizará C++ y Chromium Embedded Framework
(CEF).

La propuesta inicial contemplaba comparar Qt 6 Widgets y CEF Views mediante
un prototipo técnico antes de elegir la tecnología de interfaz.

Al comenzar Desktop Foundation, el usuario decide adoptar directamente
Qt 6 Widgets para construir una interfaz propia, atractiva, moderna y
personalizable. Esta decisión sustituye la comparación prevista.

## Opciones consideradas

### Qt 6 Widgets

Utilizar Qt 6 Widgets para construir la interfaz e integrar el navegador CEF.

Permite definir los controles, la distribución y el estilo visual de la
aplicación. Requiere integrar los eventos y el ciclo de vida de Qt y CEF,
y preparar las dependencias de ambas tecnologías para su distribución.

### CEF Views

Utilizar el framework Views de CEF para construir la interfaz
y alojar el navegador mediante sus componentes.

Era la otra alternativa prevista. No se ha construido un prototipo ni se
han medido sus controles, personalización o esfuerzo de integración frente
a Qt 6 Widgets.

## Decisión

Adoptar Qt 6 Widgets para la ventana principal y los controles propios de
SecureBrowser. CEF se integra directamente como motor de carga y renderizado
de las páginas web dentro de la ventana Qt.

La elección responde a la prioridad de diseñar una interfaz con identidad
visual propia. El aspecto moderno se construirá mediante el diseño de los
controles, colores, tipografía, iconos y espaciado; no se obtiene
automáticamente al utilizar Qt.

La decisión se toma por elección expresa del usuario, sin realizar un spike
comparativo con CEF Views. No acredita una superioridad medida ni una
integración técnica ya validada.

El desarrollo comienza con una ventana mínima y avanza hacia el navegador
CEF embebido. La interfaz completa y su personalización visual se incorporarán
de forma progresiva en sus tareas correspondientes.

## Consecuencias

- La GUI queda definida como Qt 6 Widgets y se elimina el spike comparativo
  con CEF Views del plan de Foundation.
- La aplicación debe coordinar los eventos, los hilos y el ciclo de vida de
  Qt y CEF, especialmente durante la creación y el cierre del browser.
- La compilación y distribución deben contemplar las dependencias y los
  recursos de Qt y CEF, junto con sus condiciones de licencia.
- La versión exacta de Qt, la estrategia de eventos y los detalles de
  construcción y runtime se concretarán antes de su implementación en la
  tarea #78.

## Validación

La elección de GUI está aceptada. La validación técnica permanece pendiente
y se realizará de forma incremental con Qt 6 Widgets y CEF en Windows:

- Mostrar primero una ventana Qt mínima y después un navegador CEF embebido.
- Cargar una URL introducida por el usuario.
- Probar navegación básica, redimensionamiento y foco del teclado.
- Comprobar el cierre de la ventana y de los procesos asociados.
- Comprobar la compilación y distribución con CMake en Debug y Release.
- Registrar el esfuerzo de integración y las limitaciones encontradas.
- Revisar las condiciones de licencia de las dependencias utilizadas.

Se documentarán las versiones, las condiciones de las pruebas
y los resultados conforme se completen las tareas. Este ADR no presenta
esas verificaciones como realizadas.

## Referencias

- [Arquitectura general](../architecture/overview.md).
- [Qt Widgets](https://doc.qt.io/qt-6/qtwidgets-index.html).
- [CEF Views: CefBrowserView](https://github.com/chromiumembedded/cef/blob/master/include/views/cef_browser_view.h).
- [Tarea #77: elección de Qt 6 Widgets](https://github.com/CristianPG1993/securebrowser-platform/issues/77).
- [Tarea #78: entorno e integración CEF](https://github.com/CristianPG1993/securebrowser-platform/issues/78).
