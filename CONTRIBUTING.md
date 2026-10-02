# Guía de trabajo de SecureBrowser Platform

## Objetivo

Este documento define el workflow de ramas y Pull Requests de SBP.

El código y los nombres de archivos se escriben en inglés.
La documentación se redacta en español.

## Rama principal


`main` debe permanecer estable. Contiene los cambios terminados,
revisados e integrados del proyecto.

Los cambios se realizan en ramas de trabajo y se incorporan a `main`
mediante Pull Requests. No se hacen commits ni pushes directos a `main`.

La protección de `main` se configura por separado en GitHub.

## Protección de main

La rama `main` está protegida mediante el ruleset `protect-main`,
configurado en GitHub.

| Configuración | Valor |
|---|---|
| Estado | Active |
| Rama de destino | main |
| Lista de bypass | Vacía |
| Require a pull request before merging | Activado |
| Required approvals | 0 |
| Require conversation resolution before merging | Activado |
| Restrict deletions | Activado |
| Block force pushes | Activado |
| Require status checks to pass before merging | Pendiente de configurar al incorporar CI |

La lista de bypass vacía hace que el workflow mediante Pull Requests
también se aplique al administrador del repositorio.

No se exigen aprobaciones externas porque SBP se desarrolla como
proyecto individual. El autor revisa el diff y realiza las comprobaciones
correspondientes antes de fusionar. Las conversaciones de revisión
deben estar resueltas.

Actualmente no hay workflows de CI configurados. Cuando se incorporen,
se añadirán sus checks obligatorios al ruleset. Mientras tanto,
las comprobaciones manuales se documentan en cada Pull Request.

[Configuración del ruleset protect-main](<https://github.com/CristianPG1993/securebrowser-platform/settings/rules/24382571>).

Para comprobar la configuración, abrir el ruleset en GitHub y verificar
su estado Active, el destino main, la lista de bypass vacía y las reglas
indicadas en esta tabla.

Esto deja registrada la configuración y distingue las reglas activas de los checks que añadiremos cuando exista CI.

## Ramas de trabajo

Cada tarea utiliza una rama creada desde `main` actualizado.

El nombre debe describir el propósito del cambio. Ejemplos:

- `docs/branch-pr-workflow`: documentación.
- `feature/device-enrollment`: nueva funcionalidad.
- `fix/event-deduplication`: corrección de un error.
- `test/license-capacity`: pruebas.

Cada rama debe mantener el alcance de su tarea. Los cambios ajenos
se gestionan en otra rama y otra Pull Request.

## Commits

Antes de crear un commit:

1. Revisar los archivos modificados y sus diferencias.
2. Comprobar que no se incluyen secretos ni archivos locales.
3. Ejecutar las comprobaciones adecuadas al cambio.
4. Preparar únicamente los archivos correspondientes a la tarea.

Los mensajes deben describir el cambio. Ejemplo:

`docs: document branch and pull request workflow`

## Pull Requests

Después de publicar la rama en GitHub, abrir una Pull Request
con `main` como rama de destino.

La Pull Request debe incluir:

- Un título que describa el cambio.
- Su propósito y los cambios realizados.
- La tarea del Kanban o issue relacionada, si existe.
- Las comprobaciones realizadas y sus resultados.
- Las limitaciones o asuntos pendientes relevantes.

Mientras el trabajo esté incompleto, mantener la Pull Request como
borrador.

## Revisión y fusión

Antes de fusionar una Pull Request:

1. Revisar el diff completo.
2. Confirmar que el cambio cumple el objetivo de la tarea.
3. Resolver los conflictos, si existen.
4. Comprobar los resultados de las verificaciones disponibles.
5. Resolver los comentarios de revisión pendientes.

En este proyecto individual, el autor realiza la revisión de su propio
cambio. Si participan otros colaboradores, también pueden revisarlo.

Para documentación, comprobar el contenido, los enlaces y la
visualización de los diagramas afectados. Para código, ejecutar las
pruebas y comprobaciones correspondientes.

Cuando una Pull Request ejecute workflows de GitHub Actions,
sus comprobaciones deben finalizar correctamente antes del merge.
No se fusiona mientras haya comprobaciones pendientes o fallidas.

Mientras no existan workflows configurados, documentar las
comprobaciones manuales realizadas en la Pull Request.

Fusionar mediante GitHub cuando el cambio esté terminado y revisado.

## Cierre de la tarea

Después de fusionar:

1. Confirmar que el cambio aparece en `main`.
2. Actualizar el Kanban y cerrar el issue relacionado, si corresponde.
3. Eliminar la rama de trabajo cuando ya no sea necesaria.
4. Actualizar el `main` local antes de comenzar otra tarea.