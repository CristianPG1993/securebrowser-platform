# AND-03 — Validación del cliente REST de Android

El 20 de noviembre de 2026 verifiqué que el cliente de login distingue un rechazo
del backend de un fallo de conexión. Usé Windows, Docker Compose y el emulador
`Medium_Phone_API_36.1` con la variante `debug`.

| Comprobación | Resultado observado |
| --- | --- |
| API desde Windows | Con PostgreSQL y el backend en estado `healthy`, consulté `GET /api/v1/users/me` en `127.0.0.1:8081`. Sin token recibí `401 AUTHENTICATION_REQUIRED`. Con la cuenta de demostración obtuve `200` en el login y `200` al consultar mi perfil con el token. |
| Login desde el emulador | Ejecuté `LoginBackendTest` contra `http://10.0.2.2:8081/api/v1/` con credenciales ficticias. Pasó al recibir `401 INVALID_CREDENTIALS` y convertirlo en `ApiResult.HttpError`. El código y las pruebas corresponden a `d42eab1`. |
| Contrato del login | Pasaron los cinco casos de `AuthApiTest` con MockWebServer. Verifiqué método, ruta, cuerpo JSON y tokens en el caso de éxito, el código de un `401`, un `503` sin JSON válido, la desconexión y un `200` con campos obligatorios ausentes. |
| Compilación y análisis | En `150182d` compilé los APK de desarrollo e instrumentación. Pasaron las ocho pruebas JVM del módulo y Lint terminó con cero errores y nueve avisos de versiones disponibles. |

Para repetir la validación local desde la carpeta `android` utilicé:

```powershell
.\gradlew.bat :app:assembleDebug :app:testDebugUnitTest :app:lintDebug :app:assembleDebugAndroidTest
```

Para la comprobación con Docker ejecuté `loginWithUnknownUserReturnsInvalidCredentials`
desde Android Studio con el emulador seleccionado. La autenticación con una
cuenta válida desde Android queda pendiente para la pantalla de Login.
