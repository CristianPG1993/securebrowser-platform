# AND-07 — Consulta de políticas de seguridad

## Catálogo y detalle con la cuenta demo ADMIN

El 22 de noviembre de 2026 abrí «Catálogo de políticas» desde el panel de
`ADMIN` en el emulador Android. Vi la tarjeta «Política demo verificada».
Al pulsarla entré en «Detalle de la política» y comprobé que el nombre
coincidía con el de la tarjeta.

| Control | Estado | Modo | Reglas que vi en el detalle |
| --- | --- | --- | --- |
| Filtro de URL | Activado | Lista bloqueada | `blocked.example`, `phishing.example` |
| Control de descargas | Activado | Lista bloqueada | `bat`, `exe` |

En Postman localicé «Política demo verificada» en el catálogo con el ID `1`.
Consulté `GET /api/v1/policies/1` con la sesión de administrador y obtuve
`200 OK`. Comparé el JSON con el detalle del emulador. Coincidieron el nombre,
los dos controles activados (`true`), sus modos (`DENYLIST`) y las reglas
de dominios y extensiones de la tabla.

Pulsé «Volver al catálogo» y volví a ver la tarjeta. La abrí por segunda
vez y usé Atrás de Android desde el detalle. En ambos casos regresé al
catálogo.

## Política asignada a un dispositivo con la cuenta demo USER

El 22 de noviembre de 2026 abrí «Dispositivos» con la cuenta `USER` y anoté
la última comunicación de «Desktop Demo» (ID `1`, activo). Pulsé «Ver política»
y vi «Política demo verificada» con los mismos filtros y reglas que en el catálogo
de `ADMIN`. Al pulsar «Volver a dispositivos», comprobé la nueva fecha en la
tarjeta del mismo dispositivo.

| Momento de la prueba | Última comunicación (`lastSeenAt`, UTC) |
| --- | --- |
| Antes de pulsar «Ver política» | `2026-10-24T09:35:46.077302Z` |
| Al volver a «Dispositivos» | `2026-11-22T16:38:25.887419Z` |

Con este recorrido comprobé que consultar la política asignada mediante
`GET /api/v1/devices/1/policy` actualiza la última comunicación y que la app
muestra el cambio al regresar al listado.

## Permisos del catálogo y de la política asignada

En Postman inicié sesión con la cuenta demo `USER` y obtuve `200 OK`.
Con ese token envié la petición «USER no accede al catálogo Policy».
El backend devolvió `403 Forbidden` al consultar el catálogo administrativo.

Con la sesión `ADMIN` envié «ADMIN no suplanta la instalación del USER»
para «Desktop Demo» (ID `1`), que pertenece a la cuenta `USER`.
La consulta `GET /api/v1/devices/1/policy` devolvió `404 Not Found`.

Con la cuenta `USER` filtré mis dispositivos con `GET /api/v1/devices?active=false`.
La respuesta contenía `items: []`. En el entorno demo no tenía un dispositivo
propio inactivo con el que comprobar el `409` desde Postman.

Para verificar ese caso ejecuté la prueba de integración del backend
`DeviceApiPersistenceTest#adminMustAlsoOwnInstallationAndInactiveCheckDoesNotLeakExistence`.
Con el dispositivo inactivo creado por la prueba, la consulta de su propietario
devolvió `409 Conflict` y el código `DEVICE_INACTIVE`. Maven terminó con
`BUILD SUCCESS`. El informe de Surefire registró una prueba ejecutada, cero
fallos, cero errores y cero pruebas omitidas.
