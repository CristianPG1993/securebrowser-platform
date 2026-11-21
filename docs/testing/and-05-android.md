# AND-05 — Perfil del usuario en el Dashboard

El 21 de noviembre de 2026 probé en el emulador la variante `debug` de la rama
`feature/android-dashboard`. El panel muestra los datos de la cuenta autenticada
que devuelve Spring Boot en `GET /api/v1/users/me`.

1. Inicié sesión con `user@demo.local` y comprobé que «Panel de usuario» mostraba
   el correo de esa cuenta y el rol `USER`.
2. Pulsé «Cerrar sesión» e inicié sesión con `admin@demo.local`. Comprobé que
   el panel mostraba el correo de la nueva cuenta y el rol `ADMIN`.
3. Cerré la sesión de la cuenta `ADMIN`. Cuando apareció el formulario de login,
   pulsé «Atrás» en el emulador y salí de la app sin volver al Dashboard privado.
4. Con otra sesión abierta, dejé la app en segundo plano y detuve Spring Boot.
   Al volver al Dashboard vi «No se pudo cargar tu perfil» y el botón «Reintentar».
   Arranqué Spring Boot y pulsé «Reintentar». Volví a ver mi nombre, correo y rol
   sin introducir de nuevo las credenciales.

## Navegación por rol

Con la cuenta `USER` vi «Dispositivos» y «Eventos de seguridad». El acceso a
«Catálogo de políticas» quedó oculto. Abrí cada destino y pulsé «Atrás» en el
emulador. En ambas ocasiones volví al Dashboard.

Con la cuenta `ADMIN` vi los tres accesos. Entré en «Dispositivos», «Catálogo de
políticas» y «Eventos de seguridad». Desde cada destino pulsé «Atrás» y volví
al Dashboard.
