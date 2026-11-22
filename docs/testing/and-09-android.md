# AND-09 — Consulta de eventos de seguridad

## Historial y filtro por tipo con la cuenta demo USER

Abrí «Eventos de seguridad» desde el panel de USER en el emulador. Con «Tipo:
Todos» vi dos eventos de «Política actualizada», ambos del dispositivo 1.
Probé las tres opciones del selector y observé estos resultados:

| Tipo seleccionado | Eventos visibles |
| --- | --- |
| Descarga bloqueada | 0 |
| URL bloqueada | 0 |
| Política actualizada | 2 |

Al seleccionar «Política actualizada», vi las mismas dos tarjetas que con
«Todos», en este orden:

| Dispositivo | Fecha del evento (UTC) |
| --- | --- |
| 1 | `2026-10-24T09:35:45.824Z` |
| 1 | `2026-10-24T09:32:57.299Z` |

## Historial después de ejecutar la colección de Postman

En el resumen del Run vi seis pruebas de eventos en PASS. USER envió un evento
y ADMIN pudo consultarlo. Reenvié el mismo evento y el historial de USER
conservó una sola copia y la primera fecha de recepción. También pasaron las
pruebas que esperan un conflicto al reutilizar el UUID con otro contenido y
que comprueban el rechazo del envío de ADMIN desde la instalación de USER.

Después del Run volví a abrir «Eventos de seguridad» con «Tipo: Todos».
Vi tres tarjetas de «Política actualizada» del dispositivo 1. La primera
mostraba `2026-11-22T18:07:29.996Z`; las otras dos conservaban las fechas
del 24 de octubre registradas arriba. Los tres eventos cabían en una página.
Comprobé en el emulador que el evento nuevo se veía tanto con USER como con ADMIN.

En el JSON de «USER consulta historial y verifica primera recepción» identifiqué
el evento con `id = 7`. Comparé `deviceId = 1`, `type = POLICY_UPDATED` y
`occurredAt = 2026-11-22T18:07:29.996Z` con la tarjeta y coincidieron los tres
valores. Postman devolvió `totalElements = 1` porque la petición filtraba por
dispositivo, tipo y fecha inicial del evento del Run. En Android, «Todos»
incluía también los dos eventos de octubre.

## Paginación de la API con USER

En Postman consulté `GET /api/v1/security-events?page=0&size=1`. La primera
página devolvió el evento `7`, con `totalElements = 3` y `totalPages = 3`.
Al cambiar únicamente a `page=1`, la segunda página devolvió el evento `4`.
En Android los tres eventos cabían en la primera página de 20, por lo que la
prueba en el emulador cubrió una única página.
