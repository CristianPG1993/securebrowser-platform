[CmdletBinding()]
param(
    # Solo revisa el kit existente; no necesita Python ni descarga paquetes.
    [switch]$CheckOnly,

    # Esta carpeta contiene los directorios de versiones, como C:/Qt/6.12.0.
    [ValidateNotNullOrEmpty()]
    [string]$QtDirectory = (Join-Path $PSScriptRoot '../.deps/qt'),

    [ValidateNotNullOrEmpty()]
    [string]$PythonExecutable = 'python'
)

$ErrorActionPreference = 'Stop'

# Revisa los archivos del kit que utilizan la compilación y el despliegue de Desktop.
function Assert-QtKit {
    param([string]$Root, [string]$ExpectedVersion)

    $requiredFiles = @(
        'lib/cmake/Qt6/Qt6Config.cmake',
        'include/QtCore/qconfig.h',
        'mkspecs/qconfig.pri',
        'bin/windeployqt.exe'
    )
    # Qt entrega las bibliotecas de Debug y Release juntas en el mismo kit.
    foreach ($suffix in @('', 'd')) {
        foreach ($module in @('Core', 'Gui', 'Widgets', 'Svg')) {
            $requiredFiles += "bin/Qt6$module$suffix.dll"
            $requiredFiles += "lib/Qt6$module$suffix.lib"
        }
        $requiredFiles += "plugins/platforms/qwindows$suffix.dll"
        $requiredFiles += "plugins/imageformats/qsvg$suffix.dll"
        $requiredFiles += "plugins/iconengines/qsvgicon$suffix.dll"
    }
    # Los inventarios oficiales acompañan a los módulos que se copian al runtime.
    foreach ($module in @('qtbase', 'qtsvg', 'qttranslations')) {
        $requiredFiles += "sbom/$module-$ExpectedVersion.spdx.json"
    }
    foreach ($relativePath in $requiredFiles) {
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf) -or
            (Get-Item -LiteralPath $filePath).Length -eq 0) {
            throw "El kit Qt está incompleto: falta el archivo o está vacío: $filePath"
        }
    }

    $configurationHeader = Get-Content -LiteralPath (Join-Path $Root 'include/QtCore/qconfig.h') -Raw -Encoding UTF8
    $versionMatch = [regex]::Match($configurationHeader, '(?m)^#define\s+QT_VERSION_STR\s+"([^"]+)"')
    if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -ne $ExpectedVersion) {
        throw "La versión del kit Qt no coincide con $ExpectedVersion. Kit: $Root"
    }
    $buildConfiguration = Get-Content -LiteralPath (Join-Path $Root 'mkspecs/qconfig.pri') -Raw -Encoding UTF8
    if ($buildConfiguration -notmatch '(?m)^QT_ARCH\s*=\s*x86_64\s*$' -or
        $buildConfiguration -notmatch '(?m)^QT_MSVC_MAJOR_VERSION\s*=\s*19\s*$') {
        throw "Se requiere un kit Qt de MSVC para x64. Kit: $Root"
    }
}

try {
    $desktopRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($toolchain.schemaVersion -ne 1 -or $toolchain.architecture -ne 'x64') {
        throw 'El manifiesto de CI debe utilizar schemaVersion 1 y arquitectura x64.'
    }
    $qt = $toolchain.qt
    $QtDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($QtDirectory)
    $kitRoot = Join-Path $QtDirectory "$($qt.version)/$($qt.kitDirectory)"

    if (-not (Test-Path -LiteralPath $kitRoot -PathType Container)) {
        if ($CheckOnly) {
            throw "No existe el kit Qt requerido: $kitRoot"
        }

        # aqt se instala en un entorno aislado del repositorio, con su versión fijada.
        $pythonPath = (Get-Command $PythonExecutable -CommandType Application -ErrorAction Stop).Source
        $aqtEnvironment = Join-Path $desktopRoot ".deps/tools/aqt-$($qt.aqtVersion)"
        $aqtPython = Join-Path $aqtEnvironment 'Scripts/python.exe'
        if (-not (Test-Path -LiteralPath $aqtPython -PathType Leaf)) {
            & $pythonPath -m venv $aqtEnvironment
            if ($LASTEXITCODE -ne 0) {
                throw 'No se pudo crear el entorno Python para aqtinstall.'
            }
        }
        & $aqtPython -m pip install --disable-pip-version-check "aqtinstall==$($qt.aqtVersion)"
        if ($LASTEXITCODE -ne 0) {
            throw "No se pudo preparar aqtinstall $($qt.aqtVersion)."
        }

        # Solo descargamos los archivos necesarios; aqt conserva su verificación de hashes.
        New-Item -ItemType Directory -Path $QtDirectory -Force | Out-Null
        $arguments = @(
            '-m', 'aqt', 'install-qt', 'windows', 'desktop', $qt.version,
            $qt.archiveArchitecture, '--outputdir', $QtDirectory, '--archives'
        ) + @($qt.archives)
        # Los registros de aqt quedan junto a la instalación, dentro de .deps por defecto.
        Push-Location -LiteralPath $QtDirectory
        try {
            & $aqtPython @arguments
            if ($LASTEXITCODE -ne 0) {
                throw 'La descarga o extracción del kit Qt ha fallado.'
            }
        } finally {
            Pop-Location
        }
    }

    # Una copia existente o recuperada de caché debe superar las mismas comprobaciones.
    Assert-QtKit -Root $kitRoot -ExpectedVersion $qt.version
    Write-Host "Kit Qt $($qt.version) MSVC x64 preparado: $kitRoot"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

exit 0
