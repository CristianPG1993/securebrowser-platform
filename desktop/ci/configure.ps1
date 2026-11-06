[CmdletBinding()]
param(
    [string]$QtKitDirectory,
    [string]$QtLicenseDirectory,
    [string]$CefRoot,

    [ValidateNotNullOrEmpty()]
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../build')
)

$ErrorActionPreference = 'Stop'
$exitCode = 1
try {
    $desktopRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($toolchain.schemaVersion -ne 1) {
        throw 'El manifiesto de CI debe utilizar schemaVersion 1.'
    }

    # La versión se seleccionará al preparar CI; aquí exigimos utilizar la fijada en el manifiesto.
    $cmakePath = (Get-Command cmake -CommandType Application -ErrorAction Stop).Source
    $versionOutput = @(& $cmakePath --version)
    if ($LASTEXITCODE -ne 0 -or $versionOutput.Count -eq 0 -or
        $versionOutput[0] -ne "cmake version $($toolchain.cmakeVersion)") {
        throw "Se requiere CMake $($toolchain.cmakeVersion). Ejecutable seleccionado: $cmakePath"
    }

    # CI preparará Qt dentro de .deps; una instalación local puede entregar sus propias rutas.
    if ([string]::IsNullOrWhiteSpace($QtKitDirectory)) {
        $QtKitDirectory = Join-Path $desktopRoot ".deps/qt/$($toolchain.qt.version)/$($toolchain.qt.kitDirectory)"
    }
    $QtKitDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($QtKitDirectory)
    if ([string]::IsNullOrWhiteSpace($QtLicenseDirectory)) {
        # aqt entrega el kit sin la carpeta común Licenses del instalador gráfico.
        # CI utiliza los textos conservados para esta versión de Qt en el repositorio.
        $QtLicenseDirectory = Join-Path $desktopRoot "third_party/qt/$($toolchain.qt.version)/licenses"
    }

    # El manifiesto CEF existente conserva la versión y el nombre de su directorio extraído.
    if ([string]::IsNullOrWhiteSpace($CefRoot)) {
        $cefManifest = Get-Content -LiteralPath (Join-Path $desktopRoot 'cef-sdk.json') -Raw -Encoding UTF8 |
            ConvertFrom-Json
        $sdkDirectoryName = $cefManifest.archive -replace '\.tar\.bz2$', ''
        $CefRoot = Join-Path $desktopRoot ".deps/cef/$sdkDirectoryName"
    }
    # Resolvemos también las rutas relativas respecto a la ubicación actual de PowerShell.
    $BuildDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
    $QtLicenseDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($QtLicenseDirectory)
    $CefRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CefRoot)

    # Las rutas se entregan como argumentos independientes; CMake valida las dependencias seleccionadas.
    # targetVersion es el identificador del SDK que reconoce el generador, distinto del paquete instalador.
    $arguments = @(
        '-S', $desktopRoot,
        '-B', $BuildDirectory,
        '-G', $toolchain.generator,
        '-A', "$($toolchain.architecture),version=$($toolchain.windowsSdk.targetVersion)",
        '-T', $toolchain.toolset,
        "-DCMAKE_CONFIGURATION_TYPES:STRING=$($toolchain.configurations -join ';')",
        "-DQt6_DIR:PATH=$(Join-Path $QtKitDirectory 'lib/cmake/Qt6')",
        "-DSBP_QT_LICENSE_DIR:PATH=$QtLicenseDirectory",
        "-DCEF_ROOT:PATH=$CefRoot"
    )
    & $cmakePath @arguments
    # Propagamos el resultado de CMake para que un fallo detenga el paso de GitHub Actions.
    $exitCode = $LASTEXITCODE
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $exitCode = 1
}

exit $exitCode
