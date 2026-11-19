# ADR-0016: Arquitectura y alcance del MVP Android

- Estado: Aceptada
- Fecha: 2026-11-19

## Contexto

La aplicación móvil de SBP permitirá consultar dispositivos, políticas y eventos
con la identidad y los permisos del usuario autenticado. La demostración del TFG
utilizará la API Spring Boot existente y PostgreSQL local. Las pantallas se
construirán por incrementos que se puedan ejecutar, explicar y evaluar.

## Opciones consideradas

| Decisión | Alternativas y consecuencias |
| --- | --- |
| Organización | Un módulo con paquetes por responsabilidad permite recorrer el flujo de una petición en un solo proyecto. Separar módulos Gradle puede imponer límites de compilación, con mayor coste de configuración. |
| Inyección | La construcción manual hace visibles las dependencias y permite sustituirlas en pruebas. Hilt automatizaría parte del trabajo si aumentara el número de componentes. |
| JSON | Moshi y Kotlin Serialization permiten representar los DTO de la API. Elegimos Kotlin Serialization para definir los DTO en Kotlin y generar sus serializadores mediante el plugin de compilación. |
| Sesión | Guardar los tokens con protección del sistema permitiría recuperar la sesión tras la muerte del proceso. Mantenerlos en memoria simplifica su ciclo de vida y exige volver a identificarse al arrancar otro proceso. |
| Datos locales | Una caché con Room permitiría consultar datos offline, pero exigiría decidir su vigencia, el aislamiento entre cuentas y el borrado al salir. Para el recorrido conectado basta la API. |

## Decisión

### Flujo de una pantalla

Usaremos un módulo Gradle `app` bajo `android/` y el paquete base
`com.securebrowser.platform.android`. Una Activity alojará las pantallas de
Jetpack Compose y Navigation Compose gestionará los destinos. Aplicaremos MVVM
con inyección manual. Los paquetes se incorporarán cuando los requiera cada pantalla.

Por ejemplo, al abrir Devices, el ViewModel solicita una página al repositorio
desde una coroutine. Retrofit describe la petición, OkHttp la transporta y
Kotlin Serialization interpreta el JSON. El ViewModel publica el resultado en
un `StateFlow` de estado de pantalla. El composable observa ese estado según su
ciclo de vida y representa carga, datos, lista vacía o error.

| Paquete | Responsabilidad |
| --- | --- |
| `ui/navigation` y paquetes de pantalla bajo `ui/` | Destinos, composables, ViewModels y estado de cada pantalla. |
| `data/remote` | Interfaces Retrofit y DTO contrastados con los controllers y DTO implementados. |
| `data/repository` | Ofrecer a los ViewModels operaciones como consultar dispositivos o guardar un cambio de política. |
| `core/network` | Configuración HTTP y tratamiento común de Problem Details. |
| `core/session` | Mantener el par de tokens en memoria y coordinar su uso durante la sesión. |

### Sesión y acceso

Para el MVP Android, este ADR establece una excepción a la previsión de
almacenamiento seguro del sistema del ADR-0008. Ambos tokens residirán solo
en memoria del proceso. Contraseñas y tokens quedarán fuera del estado
restaurable de Android, los logs y las evidencias.

Desde el primer Login, una sesión caducada conducirá de nuevo a Login. Hasta
implementar la renovación, un `401 AUTHENTICATION_REQUIRED` de una operación
protegida de la sesión vigente descartará sus tokens y datos privados y
retirará sus destinos del historial de navegación. Un `403` se mostrará como
falta de permiso y un fallo de red como error de conexión.

La renovación automática podrá incorporarse después de las primeras consultas.
Si varias peticiones protegidas reciben `401 AUTHENTICATION_REQUIRED` por el
mismo token, compartirán una única renovación. El cliente sustituirá ambos
tokens juntos y reintentará cada petición afectada como máximo una vez.
Un `401 INVALID_CREDENTIALS` en el login se mostrará en el formulario.
Un `401 INVALID_REFRESH_TOKEN` al renovar exigirá un nuevo login.
Las respuestas pendientes de una sesión anterior se descartarán tras salir o
cambiar de cuenta. El logout solicitará la revocación al backend y eliminará
la sesión local. Sin conexión solo podrá confirmarse el cierre local.

### Alcance y orden funcional

| Paso | Comportamiento observable |
| --- | --- |
| Login | Autenticar con email y contraseña mediante `POST /auth/login`. Todas las rutas de esta tabla usan el prefijo `/api/v1`. |
| Dashboard | Consultar `GET /users/me` y mostrar identidad, rol y accesos correspondientes. El backend verifica los permisos en cada operación. |
| Devices | Listar con `GET /devices` los dispositivos de la compañía para ADMIN y los propios para USER. |
| Policies y Policy Detail | ADMIN consulta `GET /policies` y `GET /policies/{id}`. USER abre desde Devices la política de un dispositivo propio y activo mediante `GET /devices/{id}/policy`. El detalle muestra la configuración y las reglas de URL Filtering y Download Control. |
| Events | Consultar `GET /security-events` con la paginación del backend y mostrar tipo, dispositivo y fecha dentro del ámbito autorizado. |
| Edición posterior | Tras completar las consultas básicas, ADMIN podrá guardar cambios mediante `PATCH /policies/{id}`. Se enviarán solo los campos editados. Una lista enviada sustituye todas sus reglas. |

Room queda aplazado, condicionado a comprobar si la rúbrica exige persistencia
local en Android. Cualquier necesidad de modificar Desktop o el backend congelado
deberá justificarse y contar con aprobación previa.

## Consecuencias

- La rotación de pantalla puede conservar la sesión en el mismo proceso. Su
  terminación exige otro login. El servidor sigue determinando la vigencia de los tokens.
- Una consulta correcta de la política asignada registra `lastSeenAt` en el
  dispositivo, incluso si se inició desde Android. Se realizará al abrir el
  detalle por acción del propietario. El listado de dispositivos y la consulta
  del catálogo administrativo permiten inspeccionar datos sin registrar esa comunicación.
- Las consultas requieren conexión. Una lista vacía representa una respuesta
  correcta del servidor y tendrá un estado visual distinto de un fallo de red.

## Validación

Desde la primera implementación se registrarán la revisión probada, el entorno,
el resultado observado y las limitaciones. Para la pantalla inicial bastarán
el resultado de compilación y una captura en el emulador. Las comprobaciones
siguientes son criterios de aceptación pendientes de ejecución.

1. Antes de probar la comunicación Android, arrancar Docker Compose, verificar
   los servicios y el puerto publicado, y comprobar desde el anfitrión un login
   demo y una consulta autenticada a `/users/me`. Usar ese puerto desde el
   emulador mediante `10.0.2.2`. Limitar la excepción de HTTP local al build de desarrollo.
2. Recorrer las pantallas contra esa API con ADMIN y USER. Registrar capturas y
   respuestas sin secretos, una lista vacía y un rechazo por permisos. En el
   recorrido USER, contrastar `lastSeenAt` antes y después del detalle asignado.
3. Comprobar con JUnit los estados de ViewModel y usar MockWebServer donde
   permita reproducir errores de forma determinista. Verificar la vuelta a
   Login por caducidad y, al incorporar refresh, una sola renovación ante
   peticiones concurrentes y el descarte de respuestas tras logout.
4. Al incorporar el PATCH, verificar con una nueva consulta el cambio confirmado
   y la conservación de los campos omitidos. Conservar la grabación del recorrido
   real y los resultados para la memoria y la defensa.

## Referencias

- [Convenciones del proyecto](../architecture/conventions.md)
- [Arquitectura general](../architecture/overview.md)
- [ADR-0008 sobre autenticación JWT](0008-jwt-authentication.md)
- [ADR-0009 sobre autorización por compañía](0009-company-authorization.md)
- [Contrato REST de SBP](../architecture/rest-api-contracts.md)
- [Guía del backend y entorno demo](../../backend/README.md)
