[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

try {
    $repositoryRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
    $toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($toolchain.schemaVersion -ne 1 -or $toolchain.architecture -ne 'x64') {
        throw 'El manifiesto de CI debe utilizar schemaVersion 1 y arquitectura x64.'
    }

    # La imagen estándar windows-2022 proporciona 7-Zip en esta ubicación.
    $sevenZipPath = Join-Path $env:ProgramFiles '7-Zip/7z.exe'
    $versionInfo = (Get-Item -LiteralPath $sevenZipPath -ErrorAction Stop).VersionInfo
    $expectedVersion = [version]$toolchain.sevenZipVersion
    if ($versionInfo.FileMajorPart -ne $expectedVersion.Major -or
        $versionInfo.FileMinorPart -ne $expectedVersion.Minor) {
        throw "Se requiere 7-Zip $($toolchain.sevenZipVersion); encontrado: $($versionInfo.FileVersion)."
    }
    Write-Host "7-Zip $($toolchain.sevenZipVersion) seleccionado: $sevenZipPath"

    # El módulo compartido conserva descarga, hashes, validación del SDK y limpieza.
    & (Join-Path $repositoryRoot 'scripts/setup-cef.ps1') -ArchiveExtractor SevenZip -SevenZipExecutable $sevenZipPath
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

exit 0
