# Flujo de configuración y peticiones del backend

## Alcance y estado

Estos diagramas explican la integración JWT de la tarea #51 y el flujo de
Company implementado en la rama de la tarea #44. Incluyen Controller, servicio,
repositorio, DTOs y la lectura estricta del JSON. Las pruebas específicas
completas de la API corresponden a #45 y el formato común de errores a #46.
No describen un flujo de login ni de renovación de tokens.

Los diagramas están escritos directamente en Mermaid y se visualizan al abrir
este documento en GitHub. Sus bloques son también la fuente editable.

## 1. Al arrancar: preparar los objetos

Spring carga la configuración y crea los beans. Todavía no está procesando el
JSON de una petición: está preparando los valores esperados y los componentes
que utilizará después.

```mermaid
flowchart TB
    Environment["Variables de entorno<br/>SBP_JWT_SECRET_BASE64, SBP_JWT_ISSUER y SBP_JWT_AUDIENCE"]
    MainProperties["application.properties<br/>Relaciona las propiedades sbp.security.jwt con el entorno"]
    TestProperties["Solo en pruebas sin otro perfil activo:<br/>application-default.properties aporta valores conocidos"]
    Binding["Spring carga las propiedades<br/>y construye JwtProperties"]
    Clock["Bean Clock<br/>Reloj UTC o reloj fijo de pruebas"]
    Configuration["JwtConfiguration<br/>Recibe JwtProperties y Clock"]
    Decoder["Bean JwtDecoder preparado<br/>StrictJwtDecoder utiliza NimbusJwtDecoder<br/>con JwtClaimsValidator"]

    Environment --> MainProperties
    MainProperties --> Binding
    TestProperties -. "sobrescribe los valores JWT en pruebas" .-> Binding
    Binding --> Configuration
    Clock --> Configuration
    Configuration --> Decoder
```

Por ejemplo, `SBP_JWT_ISSUER=securebrowser-backend` acaba en
`JwtProperties.getIssuer()`. Ese es el emisor **esperado** que se comparará con
el claim `iss` de cada token. El prefijo no se obtiene del JSON del JWT.
`sbp.security.jwt.max-lifetime-seconds` permite configurar la duración máxima,
con 900 segundos como valor inicial.

`JwtConfiguration` registra el decodificador; `SecurityConfiguration` lo conecta
con el proveedor y el filtro Bearer de Spring Security. Los beans se reutilizan;
el usuario y sus permisos se consultan de nuevo para cada petición autenticada.

Spring también prepara el mapper de Jackson utilizando las reglas de
`JsonConfiguration`. Ese mapper convierte entre el JSON HTTP y los DTOs;
rechaza campos desconocidos, contenido sobrante y números o booleanos donde
el DTO espera texto.

## 2. En cada petición: validar, identificar y autorizar

Este diagrama muestra el recorrido correcto de un ADMIN. Las respuestas de
rechazo se explican después para mantener legible la secuencia.

```mermaid
sequenceDiagram
    autonumber
    participant Client as Cliente
    participant Security as Spring Security
    participant JWT as Validacion JWT
    participant Identity as Identidad actual
    participant DB as PostgreSQL
    participant Company as API Company

    Client->>Security: GET o PATCH /api/v1/companies/me con Bearer JWT
    Security->>JWT: JwtAuthenticationProvider llama a decode(token)
    JWT->>JWT: StrictJwtDecoder comprueba los tipos originales
    JWT->>JWT: NimbusJwtDecoder verifica la firma HS256
    JWT->>JWT: JwtClaimsValidator comprueba claims y fechas
    JWT-->>Security: JWT validado

    Security->>Identity: AuthenticatedUserJwtConverter.convert(jwt)
    Identity->>DB: Resolver consulta UserRepository.findById(sub)
    DB-->>Identity: User con su Company y rol actuales
    Identity->>Identity: Construye AuthenticatedUser y AuthenticationToken
    Identity-->>Security: Principal y ROLE_ADMIN obtenidos de PostgreSQL
    Security->>Security: Comprueba la ruta, el metodo HTTP y el rol

    rect rgb(167, 234, 255)
        Note over Security,Company: CompanyController usa el principal autenticado
        Security->>Company: Spring proporciona @AuthenticationPrincipal
        Company->>Company: Spring lee y valida el DTO de entrada si es PATCH
        Company->>Company: CompanyService comprueba ambito y negocio
        Company->>DB: CompanyRepository consulta o guarda dentro de la transaccion
        DB-->>Company: Datos de Company y fechas auditadas
        Company->>Company: Finaliza la transaccion del servicio
        Company->>Company: Convierte el resultado a CompanyResponse
        Company-->>Client: HTTP 200 con JSON
    end
```

La columna **Identidad actual** agrupa `AuthenticatedUserJwtConverter` y
`AuthenticatedUserResolver`. El converter recibe un JWT ya validado; el resolver
consulta al usuario dentro de una transacción de solo lectura. La interacción
con PostgreSQL pasa por Spring Data JPA e Hibernate.

La columna **API Company** agrupa Controller, servicio, repositorio y DTOs.
`CompanyController` obtiene el principal mediante `@AuthenticationPrincipal` y
lo entrega a CompanyService. El servicio consulta exclusivamente por su
`companyId`, comprueba el rol ADMIN y aplica las reglas de Company. La conversión
a `CompanyResponse` ocurre después de terminar la transacción del servicio.

Por ejemplo, si Ana pertenece a la compañía 1, un GET devuelve los datos de esa
compañía. Un PATCH con `{"name":"Empresa B"}` cambia su nombre. El cliente no
elige otra compañía mediante un identificador en el cuerpo de la petición.

`AuthenticatedUser` contiene userId, companyId y role. El
`AuthenticatedUserAuthenticationToken` lo adapta a Spring Security y asigna
`ROLE_ADMIN` o `ROLE_USER`. Ese objeto interno no es otro JWT y no conserva
contraseñas ni el token como credenciales.

Los claims de rol o compañía no deciden los permisos: se utilizan los datos
actuales de PostgreSQL. Por eso un mismo JWT puede dejar de autorizar una
operación si el usuario cambia de rol o se elimina. No se guarda una sesión HTTP.

## Dónde se interrumpe una petición

| Situación | Resultado | Punto del recorrido |
| --- | --- | --- |
| No hay Bearer válido, falla la firma, el formato o los claims, o el token ha caducado | 401 con desafío Bearer | Antes de resolver el usuario. |
| La firma y los claims son válidos, pero User ya no existe | 401 con desafío Bearer | Al resolver la identidad. |
| PostgreSQL no está disponible, se agota el tiempo de espera o no se puede iniciar la transacción de resolución | 503 | Durante la resolución; no se omite esta consulta. |
| El usuario existe, pero su rol, ruta o método HTTP no están permitidos | 403 | Después de autenticar y antes del Controller. |
| El JSON de PATCH es incorrecto, tiene campos desconocidos o el nombre no supera la validación | 400 | Al leer y validar el DTO, antes de ejecutar el método del Controller. |
| La compañía no se encuentra dentro del ámbito del principal ADMIN | 404 | CompanyService lanza CompanyNotFoundException. |

`SecurityAuthenticationEntryPoint` prepara los rechazos 401 y 503 sin detalles
internos. La cadena de seguridad responde 403 a los rechazos de permisos. El
formato común de errores, incluido Problem Details, corresponde a #46.

`CompanyNotFoundException` utiliza `@ResponseStatus` para indicar el 404.
Cuando el servidor prepara una respuesta de error puede realizar un procesamiento
interno de tipo `DispatcherType.ERROR`. SecurityConfiguration permite ese paso
para conservar el estado original. Por ejemplo, un 400 continúa siendo 400.
Una petición normal del cliente a `/error` sigue bloqueada.

`SecurityHttpTest` mantiene un endpoint exclusivo de pruebas para aislar JWT
y permisos con el resolver simulado. `SecurityHttpPersistenceTest` utiliza
CompanyController y PostgreSQL reales; importa únicamente el reloj fijo para
evitar registrar también el Controller provisional. Las pruebas específicas
completas del contrato de Company se desarrollarán en #45.

## Cómo localizar cada responsabilidad

| Responsabilidad | Clases principales |
| --- | --- |
| Leer los valores esperados y preparar componentes | `JwtProperties`, `JwtConfiguration`, `SecurityConfiguration`. |
| Validar el token recibido | `StrictJwtDecoder`, `NimbusJwtDecoder`, `JwtClaimsValidator`. |
| Consultar la identidad actual | `AuthenticatedUserJwtConverter`, `AuthenticatedUserResolver`, `UserRepository`. |
| Representar la identidad y sus autoridades | `AuthenticatedUser`, `AuthenticatedUserAuthenticationToken`. |
| Decidir acceso HTTP y responder rechazos de seguridad | `SecurityConfiguration`, `SecurityAuthenticationEntryPoint`. |
| Leer JSON y validar el DTO de entrada | `JsonConfiguration`, Jackson, `CompanyUpdateRequest` y `@Valid` en el Controller. |
| Procesar Company después de autenticar | `CompanyController`, `CompanyService`, `CompanyRepository` y DTOs. |

## Referencias

- [Organización del backend](backend-structure.md).
- [Diseño de autenticación JWT](jwt-authentication.md) y [ADR-0008](../adr/0008-jwt-authentication.md).
- [Autorización por Company](company-authorization.md) y [ADR-0009](../adr/0009-company-authorization.md).
- [Contratos REST](rest-api-contracts.md).
- [Configuración y pruebas del backend](../../backend/README.md).
