# Autenticación JWT de SecureBrowser Platform

## Alcance

Este documento corresponde a la tarea #6 y define el flujo de autenticación antes
de implementar Spring Security. La decisión se recoge en el
[ADR-0008](../adr/0008-jwt-authentication.md).

El MVP corporativo utiliza email y contraseña para iniciar sesión, BCrypt para
guardar las contraseñas y JWT para autenticar las peticiones posteriores.
La renovación automática utiliza refresh tokens persistidos mediante su hash,
con rotación y revocación, para evitar repetir el login durante la jornada.
Los roles iniciales son ADMIN y USER. El usuario y su Company siguen las reglas
del [modelo de dominio](domain-model.md).

Esta tarea documenta autenticación y sus casos de prueba. La matriz de permisos
y pertenencia de recursos se recoge en la
[autorización por Company](company-authorization.md), correspondiente a la #7;
los [contratos REST](rest-api-contracts.md) corresponden a la #8.
No se crean clases ni configuración ejecutable.

## Flujo de login

1. El cliente envía email y contraseña al login mediante HTTPS.
2. El backend valida la estructura de la entrada. Normaliza el email quitando
   espacios exteriores y pasando a minúsculas, igual que al guardar User.email.
3. Busca el usuario por su email globalmente único. No se solicita Company ni
   se utiliza una Company enviada por el cliente para resolver la identidad.
4. Compara la contraseña recibida con passwordHash mediante el PasswordEncoder
   BCrypt de Spring Security. La contraseña recibida no se recorta ni normaliza.
5. Si las credenciales son correctas, crea un grupo de login, guarda el hash
   de su primer refresh token y emite el JWT. Devuelve ambos tokens después
   de confirmar la transacción. Un usuario inexistente o una contraseña
   incorrecta producen el mismo error genérico.
6. El cliente envía el token en `Authorization: Bearer <token>` al realizar
   peticiones protegidas.
7. Cuando necesita mantener el acceso al backend, el cliente utiliza su refresh
   token para obtener otro JWT y otro refresh token sin solicitar la contraseña.

La emisión corresponde al propio backend. El uso del soporte JWT de Spring
Security no introduce un proveedor externo de identidad ni un servidor OAuth
adicional para este MVP.

## Tratamiento de contraseñas

| Aspecto | Decisión |
| --- | --- |
| Almacenamiento | BCrypt, mediante BCryptPasswordEncoder de Spring Security. Solo se persiste passwordHash. |
| Coste | Valor inicial 12. Se comprobará su rendimiento antes de ajustar el coste. |
| Contraseña nueva | Al menos 12 caracteres y máximo 72 bytes al codificarla en UTF-8. |
| Normalización | Ninguna: espacios y mayúsculas forman parte de la contraseña. |
| Entrada demasiado larga | Se rechaza; no se trunca para adaptarla al límite de BCrypt. |
| Validación de credenciales | Se compara con el hash utilizando PasswordEncoder.matches; no se vuelve a generar un hash para comparar dos cadenas. |
| Exposición | Contraseñas y hashes no aparecen en respuestas ni logs. |

El mínimo se aplica al crear o cambiar una contraseña. El login comprueba que
el campo está presente y no vacío y respeta el límite de bytes, sin transformar
la contraseña recibida. El límite de bytes se comprueba expresamente: 72
caracteres no equivalen necesariamente a 72 bytes UTF-8.

BCrypt incorpora una sal para que el almacenamiento de contraseñas iguales
no dependa de hashes idénticos. El coste se mide en el entorno de ejecución;
no se considera verificado por esta revisión documental.

SHA-256 se utiliza para los secretos aleatorios de EnrollmentToken y RefreshToken;
las contraseñas siguen utilizando BCrypt.
Las operaciones de cambio de contraseña y su contrato se concretan en la #8.

## Emisión y firma del JWT

Se emite un JWT firmado con HS256. La clave es un secreto generado de forma
criptográficamente aleatoria de al menos 32 bytes, suministrado fuera del
repositorio mediante variables de entorno. Si se representa como Base64 para
configurarlo, el mínimo corresponde a los bytes decodificados.

El backend utiliza la misma clave para firmar y validar, con los componentes
JWT de Spring Security. La emisión y validación fijan HS256 como algoritmo
permitido; no se acepta el algoritmo que indique el token sin contrastarlo
con esa configuración. Se rechazan tokens sin firma o con otro algoritmo.

Este token está firmado, sin cifrar su contenido. Por eso sus claims no incluyen
contraseñas, hashes, secretos ni datos personales innecesarios.

La duración máxima inicial del JWT es de 15 minutos, configurable. El backend
calcula iat y exp con su reloj; no acepta fechas de emisión o duración del cliente.
Si al grupo de login le queda menos tiempo, exp se limita a la caducidad de ese
grupo, para que renovar cerca del final no prolongue la sesión.

El secreto, emisor, audiencia y duraciones deben tener una configuración válida
antes de emitir o aceptar tokens. La clave de firma no cambia en cada renovación;
cambian el JWT y sus fechas e identificador de emisión.

## Claims

Todos estos claims son obligatorios para los JWT emitidos y aceptados por SBP:

| Claim | Contenido | Uso |
| --- | --- | --- |
| `sub` | User.id representado como cadena. | Resolver al usuario actual por su identificador estable. |
| `iss` | Emisor configurado del backend SBP. | Aceptar únicamente tokens del emisor esperado. |
| `aud` | Audiencia configurada para la API de SBP. | Comprobar que el token está destinado a esta API. |
| `iat` | Instante de emisión como NumericDate. | Registrar cuándo se emitió. |
| `exp` | Instante de caducidad como NumericDate. | Rechazar un token que haya caducado. |
| `jti` | Identificador único generado al emitir el token. | Identificar esa emisión; no constituye una lista de revocación. |

NumericDate expresa segundos desde el inicio de la época Unix. La duración
máxima inicial entre iat y exp es de 900 segundos. El validador exige tipos válidos
y que exp sea posterior a iat; sub debe identificar un User persistido.

No se incluyen email, nombre, apellido, role ni companyId. El email identifica
al usuario durante el login y User.id lo identifica en peticiones autenticadas.
El rol y la Company se obtienen de la base de datos al procesar cada petición.

## Respuesta de login y renovación

| Campo | Tipo | Contenido |
| --- | --- | --- |
| `accessToken` | String | JWT firmado. |
| `tokenType` | String | `Bearer`. |
| `expiresIn` | Número entero | Duración efectiva del JWT en segundos; inicialmente como máximo 900. |
| `refreshToken` | String | Secreto aleatorio para la siguiente renovación. |

La respuesta usa un DTO y nunca expone la entidad User ni passwordHash.
El login y la renovación correctos devuelven HTTP 200 con este formato.
Los tokens originales solo se entregan al cliente; el refresh token se guarda
en PostgreSQL mediante su hash. Las respuestas que contienen tokens no se cachean.
Las rutas, DTOs concretos y el formato común de errores se documentan junto al
resto de la API en la #8, incluyendo las operaciones de renovación y logout.

## RefreshToken y grupo de login

El backend genera cada refresh token con 32 bytes de SecureRandom codificados
como Base64 URL-safe y guarda un hash SHA-256 hexadecimal de 64 caracteres,
único globalmente. El secreto no es un JWT ni se utiliza en peticiones normales
a dispositivos, políticas u otros recursos.

La entidad técnica RefreshToken pertenece a `auth`. Sus atributos, claves y
cardinalidad con User se definen en el
[modelo de dominio](domain-model.md#atributos-de-refreshtoken).

Cada login crea un familyId UUID nuevo. Ese valor agrupa los refresh tokens
sucesivos de esa sesión y lo asigna el servidor. Desktop y Android pueden tener
grupos independientes, sin asociarlos obligatoriamente a Device, porque el login
puede preceder al enrollment y Android no representa una instalación de Desktop.

La caducidad inicial del grupo es de 7 días desde el login, configurable. Todos
sus tokens conservan el mismo User, familyId y expiresAt. Rotar no reinicia el
plazo. Un refresh token permite renovar cuando usedAt y revokedAt son null y el
instante actual es anterior a expiresAt.

Los registros consumidos se conservan mientras el grupo pueda seguir vigente,
para reconocer su reutilización. Pueden eliminarse después de caducar el grupo.
Al eliminar un User conforme a las reglas del dominio, se eliminan sus refresh
tokens mediante cascada; una sesión no impide por sí sola ese borrado permitido.

## Renovación automática y rotación

1. El cliente presenta únicamente el refresh token para renovar. La operación
   no exige un JWT vigente ni envía uno caducado en Authorization.
2. El backend calcula el hash, resuelve el registro y su usuario y comprueba
   su estado y caducidad dentro de una transacción.
3. Si está disponible, marca usedAt, genera otro refresh token con el mismo
   familyId y expiresAt y guarda su hash. Emite un JWT para el usuario actual.
4. Confirma los cambios y entrega el nuevo par de tokens. El cliente sustituye
   el par anterior en su almacenamiento seguro.

La creación del grupo, renovación, logout y revocación por cambio de contraseña
se coordinan bloqueando la fila de User antes de modificar sus tokens. Esto
permite serializar las operaciones sin añadir otra entidad de sesión. Tras
obtener el bloqueo se vuelven a comprobar los registros y el instante actual.
El login comprueba además que las credenciales verificadas no han quedado
obsoletas por un cambio de contraseña concurrente antes de guardar el grupo.

Si llega un refresh token ya consumido, se revoca todo su grupo y se rechaza
la renovación. La revocación debe quedar confirmada antes de devolver el error;
la respuesta de rechazo no debe provocar que esa revocación se revierta.
Un token desconocido, caducado, revocado o cuyo usuario ya no existe se rechaza.

El cliente coordina una única renovación simultánea y deja que las peticiones
pendientes esperen su resultado. No renueva como consecuencia de un HTTP 403
ni repite indefinidamente una operación rechazada. Una respuesta perdida tras
consumir el refresh token puede exigir un nuevo login; presentar otra vez el
token consumido activa la protección contra reutilización.

El refresh token no se envía a la API como sustituto del JWT ni concede permisos
adicionales. La identidad, el rol y la Company proceden del usuario asociado,
sin aceptar valores alternativos enviados por el cliente.

## Validación de peticiones posteriores

Para una petición protegida, el backend:

1. Extrae el Bearer token de la cabecera Authorization.
2. Valida su estructura, algoritmo permitido y firma.
3. Comprueba los claims obligatorios, sus tipos, el emisor, la audiencia y
   la caducidad. Un claim ausente no se interpreta como una validación superada.
4. Resuelve el usuario actual por sub. Un usuario que ya no existe no se
   considera autenticado, aunque la firma y caducidad del JWT sean correctas.
5. Construye el contexto autenticado con User.id, Company.id y el rol actuales
   del usuario en la base de datos.
6. Aplica los permisos y pertenencia de la operación, según el diseño de la #7.

La comprobación temporal se basa en el reloj del backend, no en el reloj de
Desktop o Android. La tolerancia de caducidad se configura a cero para mantener
el límite definido: se rechaza el JWT cuando el instante actual alcanza exp.
Los límites se comprueban con un reloj controlado.

El sistema no guarda una sesión HTTP de autenticación ni una tabla de JWT emitidos.
Guarda los hashes y el ciclo de vida de los refresh tokens. La consulta del
usuario en cada petición permite obtener permisos actuales; no constituye una
lista de revocación inmediata de JWT.

Un cambio de email no rompe la identidad del token porque sub utiliza User.id.
Un cambio de rol se refleja en peticiones posteriores mediante la consulta del
usuario actual. No se confía en datos de permisos enviados por el cliente.

## Caducidad, cierre de sesión y cuentas

- Al caducar el JWT, el cliente renueva automáticamente mientras el refresh
  token siga disponible. El nuevo login se exige al caducar el grupo, revocarse
  o rechazarse la renovación.
- Cerrar sesión presenta el refresh token, revoca su grupo en el backend y
  elimina ambos tokens del cliente. La revocación requiere comunicación con
  el backend; borrar tokens offline solo elimina las copias locales.
- Cambiar la contraseña revoca todos los grupos de refresh tokens del usuario,
  junto con el cambio de passwordHash, en una misma transacción.
- Revocar la renovación no invalida JWT ya emitidos. Estos pueden seguir siendo
  válidos hasta caducar, como máximo otros 15 minutos con la configuración inicial,
  mientras el usuario siga existiendo.
- No se incluye deshabilitación de cuentas en el MVP: User no tiene un atributo
  active y no se añade en esta tarea. Si se incorpora esa función posteriormente,
  deberán revisarse login y validación de peticiones.
- Device.active controla la instalación y la capacidad de licencia. No representa
  el estado de la cuenta y no impide por sí solo que su User inicie sesión.
- La caducidad del JWT no elimina la política local ni los eventos pendientes
  de Desktop. El funcionamiento offline puede continuar; las comunicaciones
  protegidas se recuperan mediante renovación si el refresh sigue vigente, o
  mediante un nuevo login si el grupo ha caducado o se ha revocado.
- Los clientes conservan los tokens en almacenamiento seguro del sistema y
  sustituyen el refresh token al rotar. No guardan la contraseña para renovar.

## Errores de autenticación

| Situación | Resultado |
| --- | --- |
| Entrada de login inválida, campos ausentes o contraseña fuera del límite de bytes | HTTP 400, error de validación. |
| Email válido sin usuario o contraseña incorrecta | HTTP 401, mismo mensaje genérico de credenciales incorrectas. |
| Petición protegida sin Bearer token válido | HTTP 401. |
| Token malformado, alterado, con firma incorrecta, algoritmo no permitido, claims inválidos o caducado | HTTP 401, sin revelar detalles internos de validación. |
| JWT válido cuyo usuario ya no existe | HTTP 401. |
| Entrada de renovación sin refresh token o con estructura inválida | HTTP 400. |
| Refresh token desconocido, caducado, revocado o cuyo usuario ya no existe | HTTP 401, error genérico de renovación. |
| Refresh token consumido presentado para renovar | Revocación confirmada del grupo y HTTP 401. |
| Usuario autenticado cuyo rol no permite una operación | HTTP 403; un recurso fuera del ámbito autorizado de una operación permitida produce HTTP 404, conforme a la autorización por Company. |

Los errores de autenticación no confirman si existe un email. La respuesta no
expone contraseñas, tokens, hashes ni excepciones internas. Una indisponibilidad
de la base de datos no se presenta como credenciales incorrectas ni permite
omitir la resolución del usuario; los errores de servicio se concretan en la #8.

## Ubicación de responsabilidades

Conforme a la [estructura del backend](backend-structure.md):

- `auth`: controller, DTOs, RefreshToken, repositorio y coordinación de login,
  renovación, rotación y logout.
- `security`: integración con Spring Security, configuración de validación JWT
  y construcción del contexto autenticado a partir del usuario actual.
- `user`: persistencia del usuario y su hash BCrypt.
- `exception`: tratamiento común de errores HTTP, coordinado con los mecanismos
  de respuesta de autenticación de Spring Security.

Se utilizará el soporte de Spring Security para validar Bearer JWT y PasswordEncoder
para contraseñas. Los detalles de clases y métodos se implementan paso a paso.

## Casos de prueba previstos

| Caso | Resultado que debe comprobarse |
| --- | --- |
| Login correcto | Emite un JWT verificable y un refresh token, guarda solo su hash y devuelve tokenType y expiresIn correctos, sin passwordHash. |
| Email con mayúsculas o espacios exteriores | Resuelve el mismo usuario tras aplicar la normalización acordada. |
| Contraseña con espacios o mayúsculas | Se compara exactamente; no se normaliza ni se recorta. |
| Usuario inexistente y contraseña incorrecta | Devuelven el mismo HTTP 401 y mensaje genérico. |
| Creación o cambio de contraseña fuera de los límites | Se rechaza sin guardar ni truncar el valor. |
| Contraseña multibyte | Se comprueba el límite UTF-8, no solo el número de caracteres. |
| Emisión | Incluye los seis claims previstos y la duración configurada, sin permisos ni datos personales. |
| Firma, algoritmo, emisor o audiencia incorrectos | Rechaza la petición con HTTP 401. |
| Claims ausentes, tipos inválidos o sub no válido | No autentica la petición. |
| Caducidad del JWT | Se comprueba antes, en y después del límite de 15 minutos, con un reloj controlado y tolerancia cero. |
| Email o rol modificados | La identidad se conserva por User.id y el contexto contiene los datos actuales de la base de datos. |
| Renovación correcta | Consume el refresh anterior, entrega un nuevo par y conserva User, familyId y expiresAt. |
| Renovación con JWT caducado | Funciona presentando únicamente un refresh vigente. |
| Renovación cerca del final del grupo | El JWT no supera la caducidad original del grupo de 7 días. |
| Reutilización de refresh consumido | Revoca su grupo; al rechazar la operación, la revocación permanece confirmada. |
| Renovaciones simultáneas del mismo token | Solo una puede consumirlo; la otra detecta reutilización y revoca el grupo. |
| Refresh inválido o grupo caducado | Rechaza la renovación sin emitir tokens. |
| Logout | Revoca el grupo correspondiente; otros grupos del usuario permanecen disponibles. |
| Contraseña cambiada | Revoca todos los refresh del usuario; los JWT emitidos pueden seguir válidos hasta caducar. |
| Cambio de contraseña concurrente con renovación | No queda una renovación utilizable emitida con anterioridad al cambio. |
| Usuario eliminado | Elimina sus refresh tokens y rechaza JWT anteriores al no existir User. |
| Petición protegida sin token | HTTP 401. |

Las pruebas de permisos concretos y aislamiento entre compañías corresponden
también a la tarea #7. La revisión actual solo valida el diseño documental;
los casos anteriores se implementan y ejecutan cuando exista código.
Las garantías transaccionales de renovación y revocación se comprueban con
PostgreSQL real; los mocks no validan los bloqueos ni la confirmación del rechazo.

## Referencias

- [ADR-0008](../adr/0008-jwt-authentication.md).
- [Modelo de dominio](domain-model.md).
- [Autorización por Company](company-authorization.md).
- [Contratos iniciales de la API REST](rest-api-contracts.md).
- [Organización del backend](backend-structure.md).
- [Convenciones y configuración sensible](conventions.md).
- [JWT en Spring Security](https://docs.spring.io/spring-security/reference/servlet/oauth2/resource-server/jwt.html).
- [PasswordEncoder y BCrypt](https://docs.spring.io/spring-security/reference/features/authentication/password-storage.html).
- [Almacenamiento de contraseñas de OWASP](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).
- [RFC 7519: JWT y claims](https://www.rfc-editor.org/rfc/rfc7519.html).
- [RFC 7518: HS256](https://www.rfc-editor.org/rfc/rfc7518.html).
- [RFC 9700: protección de refresh tokens](https://www.rfc-editor.org/rfc/rfc9700.html#section-4.14).
- [Tarea #6](https://github.com/CristianPG1993/securebrowser-platform/issues/6).
