@echo off
setlocal

rem El modo de demostracion se activa de forma explicita al iniciar el EXE.
if not exist "%~dp0SecureBrowser.exe" (
    echo No se encuentra SecureBrowser.exe junto a este lanzador.
    echo Extraiga la carpeta completa del paquete antes de abrir la demostracion.
    pause
    exit /b 1
)

rem El ZIP no conserva las ACL de NTFS. Aplica el mismo permiso LPAC que exige el SDK CEF.
"%SystemRoot%\System32\icacls.exe" "%~dp0." /grant "*S-1-15-2-2:(OI)(CI)(RX)" >nul
if errorlevel 1 (
    echo No se pudieron preparar los permisos de lectura y ejecucion de CEF.
    echo Extraiga el paquete en una carpeta local donde tenga permisos de escritura.
    pause
    exit /b 1
)

start "" /D "%~dp0" "%~dp0SecureBrowser.exe" --demo-url-filtering --demo-policy=free
exit /b %errorlevel%
