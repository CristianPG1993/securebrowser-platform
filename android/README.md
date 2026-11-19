# SecureBrowser Platform para Android

## Entorno de compilación

Versiones previstas para la primera compilación del módulo `app`, según el
inventario del entorno de desarrollo del 19 de noviembre de 2026.

| Herramienta | Versión | Uso previsto |
| --- | --- | --- |
| Android Studio | Panda 3 (2025.3.3) | Abrir el proyecto Android y sincronizar Gradle. |
| JDK para Gradle | 21.0.10 (JetBrains Runtime) | Ejecutar Gradle con el JDK incluido en Android Studio. |
| Gradle | 9.3.1 | Fijar la versión de compilación mediante el Gradle Wrapper. |
| Android Gradle Plugin (AGP) | 9.1.1 | Compilar el módulo Android y empaquetar el APK. |
| Kotlin | 2.2.10 | Compilar las fuentes Kotlin con el soporte integrado en AGP. |
| Plataforma Android SDK | API 36.1 | Proporcionar las API disponibles al compilar (`compileSdk`). |
| Android SDK Build Tools | 36.0.0 | Utilizar la versión predeterminada de AGP 9.1.1. |

La combinación se ha contrastado con las tablas de compatibilidad de
[AGP](https://developer.android.com/build/releases/agp-9-1-0-release-notes) y
[Gradle](https://docs.gradle.org/current/userguide/compatibility.html).
La primera compilación Debug en AND-02 permitirá verificarla en el proyecto.
