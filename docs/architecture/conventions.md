# Convenciones de SecureBrowser Platform

## Alcance

Este documento recoge las convenciones acordadas para el proyecto.

## Nombres del proyecto

| Elemento | Nombre |
|---|---|
| Repositorio | `securebrowser-platform` |
| Nombre del proyecto | SecureBrowser Platform |
| Sigla | SBP |
| Namespace C++ | `sbp` |
| Package base Java | `com.securebrowser.platform` |
| Package base Android | `com.securebrowser.platform.android` |
| Ejecutable desktop | `SecureBrowser.exe` |
| Base de datos PostgreSQL | `securebrowser_db` |
| Servicios Docker | Prefijo `sbp-` |

## Idioma

- Los nombres de código y archivos se escriben en inglés.
- La documentación se redacta en español.

## Configuración sensible

- Los secretos no se incluyen en el código ni en el repositorio.
- La configuración sensible se proporciona mediante variables de entorno.
- Los archivos `.env` y `.env.*` se excluyen mediante `.gitignore`.
- `.env.example` puede versionarse como ejemplo sin secretos reales.

## Decisiones arquitectónicas

Las decisiones arquitectónicas relevantes se documentan mediante ADR
en `docs/adr/`, utilizando la plantilla del repositorio.

Las propuestas mantienen el estado `Propuesta` hasta que se tome la decisión.

## Referencias

- [Arquitectura general](overview.md).
- [Plantilla ADR](../adr/template.md).
- [Reglas de exclusión](../../.gitignore).

Las referencias permiten consultar los documentos que respaldan las convenciones.
