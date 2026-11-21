# AND-04 — Evidencia del formulario de Login

El 21 de noviembre de 2026 probé la pantalla de Login de la rama
`feature/android-auth-session`, sobre el repositorio de sesión de `d4f8cbb`.
Usé el emulador `Medium_Phone_API_36.1` y la variante `debug`. Docker Compose
mostraba Spring Boot y PostgreSQL en estado `healthy`, con el backend publicado
en el puerto `8081` de Windows.

| Comprobación | Resultado |
| --- | --- |
| Login con una cuenta válida | Introduje las credenciales de una cuenta válida en el emulador y pulsé Entrar. Tras la respuesta de Spring Boot, vi «Sesión iniciada» en la pantalla. |
| Login con credenciales incorrectas | Introduje credenciales ficticias mediante una prueba de Compose. Al pulsar Entrar, la app llamó a la API y mostró «El correo o la contraseña son incorrectos». La prueba del repositorio también verificó `401 INVALID_CREDENTIALS`. |
| Restauración del formulario | Tras escribir en ambos campos y restaurar el estado de Compose, comprobé que el correo y la contraseña estaban vacíos. |
| Estado de la pantalla | Verifiqué con pruebas JVM la validación de campos, el bloqueo de envíos duplicados, el estado de éxito y la distinción entre credenciales incorrectas y fallo de conexión. |
| Compilación | Compilé los APK de desarrollo y release, y Lint terminó correctamente. Pasaron 17 pruebas JVM y 4 en el emulador. |

La renovación automática queda aplazada.

## Cierre de sesión

He comprobado con MockWebServer que Android envía únicamente el campo
`refreshToken` a `POST /api/v1/auth/logout` y acepta la confirmación `204`.
Con respuestas controladas, he verificado que el dispositivo descarta sus tokens
antes de recibir la respuesta y que un cierre anterior respeta la siguiente sesión.
También he comprobado el cierre local ante un fallo de conexión o un rechazo del
servidor y que dos pulsaciones seguidas generan una sola solicitud.

Han pasado 22 pruebas JVM y Lint. He compilado el APK de desarrollo y el de
pruebas instrumentadas. En el emulador inicié sesión con una cuenta de prueba
y pulsé «Cerrar sesión». La app mostró «Sesión cerrada», el mensaje que
corresponde a la confirmación `204` de Spring Boot.

## Sesión caducada

La app consulta `GET /api/v1/users/me` al mostrar la sesión iniciada y al
regresar al primer plano. He comprobado con MockWebServer la ruta, el envío
del access token como Bearer y la recepción del `401`.

Con respuestas controladas he verificado que un `401` de la sesión actual
retira los dos tokens y devuelve al formulario, donde puedo iniciar sesión
de nuevo. También he comprobado que un `403`, un `503` y un fallo de conexión
conservan la sesión y que un `401` anterior no cierra una sesión posterior.

Han pasado las 26 pruebas JVM y Lint. He compilado el APK de desarrollo y el de
pruebas instrumentadas. Queda pendiente la comprobación manual con un JWT que
haya caducado en Spring Boot.
