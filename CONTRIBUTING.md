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
| Require status checks to pass before merging | Activado: `Backend build and tests`, origen GitHub Actions |
| Require branches to be up to date before merging | Activado |

La lista de bypass vacía hace que el workflow mediante Pull Requests
también se aplique al administrador del repositorio.

No se exigen aprobaciones externas porque SBP se desarrolla como
proyecto individual. El autor revisa el diff y realiza las comprobaciones
correspondientes antes de fusionar. Las conversaciones de revisión
deben estar resueltas.

El workflow [Backend CI](.github/workflows/backend.yml) define el check
`Backend build and tests`. El ruleset exige que ese check proceda de GitHub
Actions y se supere antes de fusionar. La rama de la PR debe estar actualizada
con `main`; si esta avanza, actualizar la rama y esperar al nuevo check.
Las comprobaciones manuales adicionales se documentan en cada Pull Request.

[Configuración del ruleset protect-main](<https://github.com/CristianPG1993/securebrowser-platform/settings/rules/24382571>).

Para comprobar la configuración, abrir el ruleset en GitHub y verificar
su estado Active, el destino main, la lista de bypass vacía y las reglas
indicadas en esta tabla.

### Validación y mantenimiento del check obligatorio

El workflow se verificó en la [PR #56](https://github.com/CristianPG1993/securebrowser-platform/pull/56)
y después de su fusión en `main`. Ambas ejecuciones completaron 805 pruebas
sin fallos, errores ni pruebas omitidas y generaron el JAR:

- [Ejecución de la PR](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/39605638848).
- [Ejecución de main](https://github.com/CristianPG1993/securebrowser-platform/actions/runs/39606084602).

Tras esa validación, el autor activó el check obligatorio en `protect-main`
el 21 de octubre de 2026. La configuración se comprobó también mediante
las reglas efectivas de `main`, conservando el resto de las protecciones.

Para revisar o volver a configurar la regla:

1. Comprobar que la PR ejecuta `Backend CI` y que su check
   `Backend build and tests` termina correctamente para la última revisión.
   Registrar el enlace a la ejecución en la PR.
2. Abrir el ruleset `protect-main` y comprobar la regla
   `Require status checks to pass before merging`.
3. Seleccionar el nombre exacto `Backend build and tests`, comprobado en
   esa ejecución, y GitHub Actions como origen esperado del check.
4. Conservar las demás reglas: estado Active, destino `main`, bypass vacío,
   PR obligatoria, cero aprobaciones externas, resolución de conversaciones,
   protección frente al borrado y bloqueo de force pushes.
5. Conservar la exigencia de actualizar la rama de la PR con `main` antes
   del merge. Si se cambia la configuración, guardar y comprobar de nuevo
   las reglas efectivas y el check requerido en una PR.

No se cambia el número de aprobaciones ni se añaden excepciones para
evitar un check fallido. La configuración de reglas se describe en la
[documentación de GitHub sobre checks obligatorios](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/available-rules-for-rulesets#require-status-checks-to-pass-before-merging).

## Ramas de trabajo

Cada tarea utiliza una rama creada desde `main` actualizado.

El nombre debe describir el propósito del cambio. Ejemplos:

- `docs/branch-pr-workflow`: documentación.
- `feature/device-enrollment`: nueva funcionalidad.
- `fix/event-deduplication`: corrección de un error.
- `test/license-capacity`: pruebas.
- `ci/backend-build-tests`: integración continua del backend.

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

## Comprobaciones de CI del backend

`Backend CI` se ejecuta en las PR hacia `main` y con los pushes a `main`.
También permite ejecución manual cuando el workflow está en la rama principal.
No utiliza filtros por rutas: una PR que solo cambie documentación también
debe obtener el check, para que su futura obligatoriedad no deje PR bloqueadas
sin una ejecución.

El job `Backend build and tests` utiliza Ubuntu 24.04, JDK 25, Maven Wrapper
3.9.12, un servicio PostgreSQL 17 con comprobación de disponibilidad y Docker
para las pruebas que utilizan Testcontainers. Su configuración de conexión
es exclusiva de CI y no requiere `.env` ni servicios del equipo del autor.

Ejecuta `./mvnw --batch-mode --no-transfer-progress verify` desde `backend`.
Debe compilar, ejecutar todas las pruebas y generar el JAR, incluyendo las
comprobaciones de Flyway e Hibernate realizadas por los tests de persistencia.
No se aceptan `skipTests`, selección parcial de tests ni opciones para ignorar
fallos como sustitutos de esta verificación completa.

Para reproducir la comprobación en PowerShell, preparar PostgreSQL, las
variables `SBP_DB_*` y Docker como indica el [README del backend](backend/README.md).
Desde `backend`:

```powershell
.\mvnw.cmd --batch-mode --no-transfer-progress verify
```

Antes del merge, revisar el resultado de la última revisión de la PR y abrir
los logs si falla. Si se publican nuevos commits, esperar al check de esa
revisión; una ejecución anterior o cancelada no valida los cambios nuevos.
Tras fusionar, comprobar también la ejecución correspondiente al push a `main`.

### Correspondencia con las decisiones de arquitectura

La configuración actual de Spring Boot 4.1.1 gestiona JUnit Jupiter 6.0.3.
El [documento de estructura del backend](docs/architecture/backend-structure.md)
ya menciona JUnit 6, mientras que el
[ADR-0007 aceptado](docs/adr/0007-backend-package-organization.md) conserva
la previsión inicial de JUnit 5. CI utiliza las dependencias actuales del POM.
La discrepancia queda identificada para su revisión; cualquier aclaración
del ADR se consultará antes de modificar esa decisión aceptada.

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

Documentar también las comprobaciones manuales que no cubra el workflow.

Fusionar mediante GitHub cuando el cambio esté terminado y revisado.

## Cierre de la tarea

Después de fusionar:

1. Confirmar que el cambio aparece en `main`.
2. Actualizar el Kanban y cerrar el issue relacionado, si corresponde.
3. Eliminar la rama de trabajo cuando ya no sea necesaria.
4. Actualizar el `main` local antes de comenzar otra tarea.
