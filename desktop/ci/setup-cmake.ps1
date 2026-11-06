[CmdletBinding()]
param(
    # Solo comprueba el CMake seleccionado actualmente en PATH.
    [switch]$CheckOnly,

    [ValidateNotNullOrEmpty()]
    [string]$ToolsDirectory = (Join-Path $PSScriptRoot '../.deps/tools')
)

$ErrorActionPreference = 'Stop'

# CMake identifica su versión en la primera línea de --version.
function Test-CMakeVersion {
    param([string]$Executable, [string]$ExpectedVersion)

    if ([string]::IsNullOrWhiteSpace($Executable) -or
        -not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
        return $false
    }
    $versionOutput = @(& $Executable --version)
    return ($LASTEXITCODE -eq 0 -and $versionOutput.Count -gt 0 -and
        $versionOutput[0] -eq "cmake version $ExpectedVersion")
}

try {
    $toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($toolchain.schemaVersion -ne 1 -or $toolchain.architecture -ne 'x64') {
        throw 'El manifiesto de CI debe utilizar schemaVersion 1 y arquitectura x64.'
    }

    # Reutilizamos la instalación seleccionada si ya coincide con la versión requerida.
    $cmakePath = (Get-Command cmake -CommandType Application -ErrorAction SilentlyContinue).Source
    if (-not (Test-CMakeVersion $cmakePath $toolchain.cmakeVersion)) {
        if ($CheckOnly) {
            throw "El CMake seleccionado en PATH no es la versión $($toolchain.cmakeVersion)."
        }

        $ToolsDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ToolsDirectory)
        $bundleName = "cmake-$($toolchain.cmakeVersion)-windows-x86_64"
        $cmakePath = Join-Path $ToolsDirectory "$bundleName/bin/cmake.exe"
        if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf)) {
            New-Item -ItemType Directory -Path $ToolsDirectory -Force | Out-Null
            $archivePath = Join-Path $ToolsDirectory "$bundleName.zip"
            # La release oficial de Kitware publica el ZIP y su lista de SHA-256.
            $archiveUrl = "https://github.com/Kitware/CMake/releases/download/v$($toolchain.cmakeVersion)/$bundleName.zip"
            if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
                Invoke-WebRequest -UseBasicParsing -Uri $archiveUrl -OutFile $archivePath -TimeoutSec 120
            }
            if ($toolchain.cmakeArchiveSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
                (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash -ne $toolchain.cmakeArchiveSha256) {
                throw "El SHA-256 del ZIP de CMake no coincide con el manifiesto: $archivePath"
            }
            Expand-Archive -LiteralPath $archivePath -DestinationPath $ToolsDirectory
        }
        if (-not (Test-CMakeVersion $cmakePath $toolchain.cmakeVersion)) {
            throw "La copia preparada de CMake no corresponde a $($toolchain.cmakeVersion): $cmakePath"
        }
    }

    $cmakeBinDirectory = Split-Path -Parent $cmakePath
    if (-not $CheckOnly) {
        # La selección afecta a este proceso de PowerShell; no cambia el PATH del sistema.
        $env:PATH = "$cmakeBinDirectory;$env:PATH"
        if ($env:GITHUB_ACTIONS -eq 'true' -and -not [string]::IsNullOrWhiteSpace($env:GITHUB_PATH)) {
            # GitHub aplicará esta ruta a los pasos siguientes del mismo job.
            [IO.File]::AppendAllText($env:GITHUB_PATH, "$cmakeBinDirectory`n", [Text.UTF8Encoding]::new($false))
        }
    }
    Write-Host "CMake $($toolchain.cmakeVersion) preparado: $cmakePath"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

exit 0
