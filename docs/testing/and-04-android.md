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

La renovación automática queda aplazada. Al incorporar la primera consulta privada
se deberá descartar la sesión caducada y volver a Login.
