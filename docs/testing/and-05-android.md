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
