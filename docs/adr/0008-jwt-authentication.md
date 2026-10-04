# ADR-0008: Autenticación con BCrypt y JWT

- Estado: Aceptada
- Fecha: 2026-10-04

## Contexto

La tarea #6 define el login y la autenticación del backend antes de implementar
Spring Security. El modelo corporativo utiliza User.email como identidad única
de login y User.id como identificador persistente, con roles ADMIN y USER.

Desktop y Android necesitan autenticar sus peticiones a la API. El MVP debe
mantener un flujo sencillo y permitir que los permisos reflejen el usuario
actual. Para evitar logins repetidos durante la jornada, el diseño aprobado
incluye renovación automática con refresh tokens. La deshabilitación de cuentas
permanece fuera del MVP.

## Opciones consideradas

| Aspecto | Opción elegida | Alternativa y compromiso |
| --- | --- | --- |
| Credenciales en peticiones posteriores | JWT Bearer de duración limitada. | Repetir email y contraseña en cada petición aumenta su exposición y el coste de validación BCrypt. |
| Firma | HS256 con secreto aleatorio de al menos 32 bytes. | Firma asimétrica separa claves de emisión y validación, con gestión adicional para el backend único del MVP. |
| Permisos | Resolver User, rol y Company en cada petición. | Incluirlos en el JWT evita esa consulta, pero puede conservar permisos anteriores hasta la caducidad. |
| Renovación | JWT corto y refresh token con rotación. | Un JWT largo sin renovación simplifica el backend, pero prolonga el acceso y exige otro login al caducar. |
| Estado de renovación | Una entidad RefreshToken y familyId para agrupar cada login. | Añadir una entidad de sesión separada permitiría más metadatos, con otro ciclo de vida que no se requiere ahora. |
| Cuentas deshabilitadas | Fuera del MVP, sin añadir User.active. | Incorporar un estado de cuenta amplía el modelo y las reglas de administración y último ADMIN. |

## Decisión

### Login y contraseñas

El login utiliza email y contraseña. El email se normaliza sin espacios
exteriores y en minúsculas, igual que al persistirlo, y se busca globalmente.
La contraseña se compara sin normalizar ni recortar mediante el PasswordEncoder
BCrypt de Spring Security. Usuario inexistente y contraseña incorrecta tienen
el mismo error genérico.

Las contraseñas nuevas requieren al menos 12 caracteres y un máximo de 72 bytes
UTF-8. Los valores demasiado largos se rechazan y no se truncan. Solo se persiste
el hash BCrypt, con coste inicial 12 sujeto a medición de rendimiento. Ninguna
respuesta ni log expone contraseña o hash.

### JWT y respuesta

El propio backend emite JWT firmados con HS256. La clave aleatoria tiene al
menos 32 bytes y se suministra mediante configuración externa al repositorio.
Emisión y validación fijan el algoritmo permitido; no se acepta un token sin
firma ni se confía en su cabecera para elegir libremente el algoritmo.

Los claims obligatorios son sub, iss, aud, iat, exp y jti. Sub es User.id como
cadena; emisor y audiencia son los valores configurados para SBP; iat y exp
son NumericDate calculados por el backend; jti identifica la emisión.
No se incluyen email, nombre, apellido, rol ni Company.

La duración máxima inicial es de 15 minutos configurable. Si al grupo de login
le queda menos tiempo, exp se limita a su caducidad. La respuesta de login y
renovación contiene accessToken, tokenType con valor Bearer, expiresIn en segundos
(inicialmente como máximo 900) y refreshToken. No contiene la entidad User.

El token se envía mediante la cabecera Authorization Bearer sobre HTTPS.
Se utiliza el soporte JWT de Spring Security, sin introducir un proveedor
externo de identidad para el MVP.

### Validación y usuario actual

En cada petición protegida se validan estructura, firma, algoritmo permitido,
claims obligatorios y sus tipos, emisor, audiencia y caducidad. La tolerancia de
caducidad se configura a cero y se comprueba su efecto en los límites.

Después se resuelve User por sub. Un usuario inexistente impide autenticar
la petición. El contexto autenticado contiene la identidad, Company y rol
actuales de la base de datos. Las reglas de autorización de recursos se
documentan por separado en la tarea #7.

No se guarda una sesión HTTP ni una tabla de JWT emitidos. Consultar al usuario
actual no constituye un mecanismo de revocación individual de JWT.

### Refresh tokens y rotación

RefreshToken es una entidad técnica de `auth` con id, user, tokenHash, familyId,
createdAt, expiresAt, usedAt y revokedAt. User puede tener cero o muchos registros.
Su Company se obtiene a través de User. No se añade una relación obligatoria con
Device, porque Android y los logins anteriores al enrollment también la utilizan.

El secreto se genera con 32 bytes de SecureRandom, codificados como Base64
URL-safe. Solo se guarda su SHA-256 hexadecimal único de 64 caracteres.
Cada login crea un familyId UUID nuevo y una caducidad de grupo de 7 días,
configurable. Los tokens rotados conservan User, familyId y expiresAt.

La renovación comprueba estado y caducidad, marca usedAt, persiste el hash
de un nuevo refresh y emite otro JWT de forma transaccional. No exige un JWT
vigente. El cliente coordina una sola renovación simultánea y sustituye ambos
tokens en almacenamiento seguro del sistema sin conservar la contraseña.

Las operaciones de login, renovación, logout y cambio de contraseña se coordinan
mediante bloqueo de User. Si se reutiliza un refresh consumido, se revoca su grupo
y se rechaza la operación. Esa revocación se confirma antes de responder con error.
Una respuesta perdida después del consumo puede exigir volver a iniciar sesión.

Los registros consumidos se conservan mientras el grupo pueda estar vigente y
pueden eliminarse al caducar. El borrado permitido de User elimina en cascada
sus registros de RefreshToken. Esta regla técnica complementa las reglas de
historial del ADR-0006 sin eliminar Devices, enrollments ni eventos.

### Caducidad y límites del MVP

Un JWT caducado permite renovación automática mientras el refresh token sea
utilizable. El grupo no prolonga sus 7 días al rotar; cuando caduca, se revoca
o se rechaza, se requiere un nuevo login.

Cerrar sesión revoca ese grupo y elimina los tokens locales. Cambiar la contraseña
revoca todos los grupos del usuario junto con el cambio de passwordHash en una
transacción. Los JWT ya emitidos pueden seguir válidos hasta caducar, como máximo
otros 15 minutos con la configuración inicial, mientras el usuario exista.

No se añade deshabilitación de cuentas ni User.active. Device.active representa
la actividad de una instalación y su ocupación de licencia; no es un estado
de cuenta ni una condición de login.

Desktop puede seguir usando su política y registrando eventos offline al
caducar el JWT; al recuperar conexión utiliza renovación o nuevo login según
el estado y vigencia del refresh token.

## Consecuencias

- El login se resuelve con el email único y las peticiones conservan una identidad
  estable aunque el email cambie.
- Los cambios de rol se reflejan al construir el contexto de peticiones posteriores.
- La autenticación depende de consultar el usuario actual en la base de datos.
- Un JWT firmado tiene contenido legible, por lo que sus claims se mantienen mínimos.
- La clave HS256 debe conservarse fuera del repositorio y tener una configuración válida.
- La renovación evita introducir credenciales cada vez que caduca el JWT.
- El backend mantiene estado de refresh tokens y el cliente conserva un secreto
  de renovación que debe sustituir al rotar.
- Logout y cambio de contraseña impiden renovar, pero no revocan inmediatamente
  JWT ya emitidos. No se incluyen cuentas deshabilitadas en el MVP.
- Las renovaciones se coordinan por User y los rechazos por reutilización deben
  conservar la revocación confirmada en PostgreSQL.
- La autorización detallada y los contratos completos de API siguen en las #7 y #8.
- Esta tarea documenta la decisión y sus casos de prueba; no implementa Spring Security.

## Validación

La revisión documental comprobará que el flujo de login, los seis claims, la
duraciones, respuesta y limitaciones coinciden con el documento de autenticación
y el modelo. Se añade RefreshToken sin añadir un estado active a User.

Durante la implementación se comprobarán login correcto e incorrecto, normalización
de email, contraseñas sin transformación, límites UTF-8, claims y caducidad con
reloj controlado, firma y audiencia inválidas, usuario inexistente y cambios
de datos o contraseña, además de rotación, reutilización, concurrencia, logout
y conservación de la caducidad original. Estas garantías requieren pruebas
con PostgreSQL real. La lista completa está en el documento de autenticación.

## Referencias

- [Autenticación JWT](../architecture/jwt-authentication.md).
- [Modelo de dominio](../architecture/domain-model.md).
- [Organización del backend](../architecture/backend-structure.md).
- [Convenciones](../architecture/conventions.md).
- [JWT en Spring Security](https://docs.spring.io/spring-security/reference/servlet/oauth2/resource-server/jwt.html).
- [PasswordEncoder](https://docs.spring.io/spring-security/reference/features/authentication/password-storage.html).
- [OWASP: almacenamiento de contraseñas](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).
- [RFC 7519](https://www.rfc-editor.org/rfc/rfc7519.html).
- [RFC 7518](https://www.rfc-editor.org/rfc/rfc7518.html).
- [RFC 9700: protección de refresh tokens](https://www.rfc-editor.org/rfc/rfc9700.html#section-4.14).
- [Tarea #6](https://github.com/CristianPG1993/securebrowser-platform/issues/6).
