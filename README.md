# SecureBrowser Platform (SBP)

Proyecto de TFG del ciclo de Desarrollo de Aplicaciones Multiplataforma (DAM).

## Objetivo

Desarrollar una plataforma de navegación segura para empresas, con políticas
centralizadas, control de dispositivos y registro de eventos de seguridad.

## Componentes previstos

- **Desktop:** navegador en C++ con Chromium Embedded Framework (CEF).
- **Backend:** API en Java con Spring Boot y PostgreSQL.
- **Android:** aplicación de administración y monitorización en Kotlin.
- **Infraestructura:** Docker, Docker Compose y GitHub Actions.

## Alcance del MVP

Las funciones principales de seguridad serán el filtrado de URL y el control
de descargas. Se complementarán con autenticación, gestión de compañías,
usuarios, licencias, dispositivos, políticas y eventos de seguridad.

## Estado actual

El modelo de dominio y las decisiones iniciales de arquitectura están
documentados. El backend dispone de una estructura mínima con Spring Boot,
Maven Wrapper y compilación, empaquetado y arranque verificados.

Los requisitos y comandos de ejecución están disponibles en el
[README del backend](backend/README.md).

## Documentación

- [Arquitectura general](docs/architecture/overview.md).
- [Modelo de dominio del MVP](docs/architecture/domain-model.md).
- [Decisiones arquitectónicas (ADR)](docs/adr/).
- [Convenciones del proyecto](docs/architecture/conventions.md).

## Derechos de uso

Todos los derechos reservados sobre el código y la documentación originales
de SecureBrowser Platform.

No se concede una licencia general para utilizar, modificar, redistribuir
o incorporar este material en otros proyectos sin autorización expresa
del titular de los derechos.

Este aviso no limita los usos permitidos por la legislación aplicable
ni los permisos concedidos mediante las condiciones de servicio de GitHub.

Las dependencias y los materiales de terceros se rigen por sus propias licencias.
