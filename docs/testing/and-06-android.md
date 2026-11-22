# AND-06 — Listado de dispositivos

## Consulta con la cuenta demo USER

El 22 de noviembre de 2026 abrí «Dispositivos» desde el Dashboard de
`user@demo.local`. En el emulador vi una tarjeta con el nombre «Desktop Demo»,
el ID `1`, el estado «Activo» y la última comunicación `2026-10-24T09:35:46.077302Z`.

Contrasté la pantalla con `GET /api/v1/devices?page=0&size=20` usando la misma
cuenta, sin filtros adicionales. La API devolvió un solo elemento en una única
página. Coincidieron el ID, el nombre, el estado y la fecha de última comunicación.

## Consulta con la cuenta demo ADMIN

Inicié sesión con `admin@demo.local`, comprobé que el Dashboard mostraba el rol
`ADMIN` y abrí «Dispositivos». Vi «Desktop Demo», ID `1`, activo, con última
comunicación `2026-10-24T09:35:46.077302Z`.

Verifiqué el rol mediante `GET /api/v1/users/me` y repetí la consulta
`GET /api/v1/devices?page=0&size=20` sin filtros adicionales. La API devolvió
un elemento en una página y sus datos coincidieron con la tarjeta del emulador.
Con estos datos demo, ambas cuentas ven el ID `1`. Para comprobar en el
emulador la diferencia de visibilidad por propietario haría falta otra
instalación de la misma compañía asignada a un usuario distinto.
