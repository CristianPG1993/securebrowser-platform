# AND-10 — Instalación y ejecución del APK Android

El recorrido de demostración utiliza el emulador de Android Studio y el backend demo de Spring Boot con PostgreSQL en el mismo PC. Los comandos están escritos para PowerShell y parten de la raíz del repositorio.

## APK de la demostración

| Dato | Valor |
| --- | --- |
| Revisión de la aplicación | `f04f8ef0204cd43bf42f0e01254ce8799378e8a6` (PR #155) |
| Paquete | `com.securebrowser.platform.android` |
| Versión | `1.0` (`versionCode` 1), variante `debug`, Android 7.0 (API 24) o superior |
| Emulador utilizado | Medium Phone API 36.1 |
| Archivo | `android/app/build/outputs/apk/debug/app-debug.apk` |
| SHA-256 del APK probado | `3E85500ED31BA42D82BD36727011F47E634A3516263C9796FCDCD4B05A23FB5D` |

## 1. Arrancar Spring Boot y PostgreSQL

Abre Docker Desktop en modo de contenedores Linux. En la primera instalación, prepara el `.env` de la raíz a partir de `.env.example` siguiendo la [guía del backend](../../backend/README.md#guía-rápida-de-pruebas). Configura **`SBP_HTTP_PORT=8081`**. El APK accede a `http://10.0.2.2:8081/api/v1/` desde el emulador.

```powershell
docker compose up --build --detach --wait --wait-timeout 180
docker compose ps
docker compose port sbp-backend 8080
```

Continúa cuando `sbp-postgres` y `sbp-backend` indiquen `healthy` y el último comando muestre `127.0.0.1:8081`.

## 2. Localizar y comprobar el APK

Si ya tienes el APK de la entrega, usa su ruta en `$apk` y compara la huella con la tabla. Para compilarlo desde el código fuente de la revisión indicada, abre el proyecto `android/` en Android Studio, sincroniza Gradle y ejecuta desde la raíz:

```powershell
.\android\gradlew.bat -p android :app:assembleDebug
```

```powershell
$apk = '.\android\app\build\outputs\apk\debug\app-debug.apk'
(Get-FileHash -Algorithm SHA256 -LiteralPath $apk).Hash
```

La huella identifica el archivo exacto que utilicé. Una compilación en otro equipo puede producir otra huella, por ejemplo si utiliza otra clave de firma debug.

## 3. Instalar y abrir en el emulador

Arranca un emulador desde **Device Manager** en Android Studio y espera a ver la pantalla de inicio de Android. Los comandos usan la ubicación habitual del SDK en Windows. Si tu SDK está en otra carpeta, ajusta `$adb`.

```powershell
$adb = "$env:LOCALAPPDATA\Android\Sdk\platform-tools\adb.exe"
& $adb devices -l
& $adb -e install -r $apk
& $adb -e shell am start -n com.securebrowser.platform.android/.MainActivity
```

En el listado debe aparecer un emulador con estado `device`. La opción `-e` lo selecciona aunque también haya un móvil conectado. La instalación debe devolver `Success`. Al abrir la app desde un proceso nuevo aparece el login.

## 4. Reproducir el recorrido con ambos roles

1. **ADMIN.** Entra con `admin@demo.local` y la contraseña demo configurada para esa cuenta. Comprueba el rol del panel, abre **Dispositivos** y vuelve al panel. En **Catálogo de políticas**, abre la política asignada a **Desktop Demo** y pulsa **Editar**. Anota las reglas actuales y añade un dominio de ejemplo y una extensión que aún no estén en sus respectivas listas. Pulsa **Guardar**, espera a ver **Cambios confirmados** y vuelve a abrir el detalle. Deben conservarse las reglas anteriores y las nuevas. Desde el panel, consulta **Eventos de seguridad**, prueba el filtro por tipo y cierra sesión.
2. **USER.** Entra con `user@demo.local` y su contraseña demo. Abre **Dispositivos → Desktop Demo → Ver política**. Comprueba que aparecen los dos cambios y que el detalle ofrece **Volver a dispositivos** y **Cerrar sesión**. Esta consulta actualiza `lastSeenAt` en el backend. Vuelve al panel, abre **Eventos de seguridad** y cierra sesión.

## Evidencias y límites de la prueba

- En la validación de AND-08 añadí `prueba-tfg.example` y `zip`, volví a consultar la política como ADMIN y comprobé ambas reglas como USER. El PATCH directo de USER recibió `403`. Una regla inválida de ADMIN recibió `400` y el GET posterior conservó la política guardada.
- Instalé el APK identificado en el emulador y en un móvil físico. En el móvil comprobé la apertura del login. El recorrido autenticado con este APK corresponde al emulador, donde `10.0.2.2` permite acceder al PC.
