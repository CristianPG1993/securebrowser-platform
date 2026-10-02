# ADR-0001: Organización de SBP en un único repositorio

- Estado: Aceptada
- Fecha: 2026-10-02

## Contexto

SecureBrowser Platform incluye tres componentes principales: Desktop,
Backend y Android, junto con documentación e infraestructura.

La preparación inicial ya ha establecido un único repositorio,
`securebrowser-platform`, con una carpeta para cada componente.

Este ADR registra esa organización y explica sus implicaciones.

## Opciones consideradas

Se compara la estructura adoptada con su alternativa principal.

### Un único repositorio

Desktop, Backend, Android, documentación e infraestructura comparten
el mismo repositorio y el mismo historial de Git.

Facilita la revisión conjunta del TFG y permite agrupar cambios relacionados
en un mismo commit o Pull Request.

### Repositorios separados

Cada componente tiene su propio repositorio e historial.

Permite gestionar accesos y flujos de trabajo por separado, pero requiere
coordinar los cambios que afectan a varios componentes y mantener referencias
entre sus repositorios.

## Decisión

Mantener la organización inicial de SBP como monorepo.

Los componentes principales se ubican en:

- `desktop/`
- `backend/`
- `android/`

La documentación y los recursos compartidos se ubican en:

- `docs/`
- `database/`
- `diagrams/`
- `docker/`
- `.github/workflows/`

Esta organización permite presentar y revisar el proyecto completo
desde un único repositorio.

## Consecuencias

- Los componentes y su documentación comparten historial.
- Los cambios relacionados pueden revisarse conjuntamente.
- Cada componente conserva sus herramientas y dependencias específicas.
- La configuración futura de CI deberá contemplar las necesidades
  de cada componente.

## Validación

- Comprobar que las carpetas previstas están versionadas.
- Comprobar que la documentación técnica está en el mismo repositorio.
- Mantener archivos `.gitkeep` mientras las carpetas estén vacías.

## Referencias

- [Arquitectura general](../architecture/overview.md).
- [Modelo de dominio](../architecture/domain-model.md).