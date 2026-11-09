# Prepara las fuentes de GoogleTest fijadas en desktop/googletest.json.
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$DependenciesDirectory = (Join-Path $PSScriptRoot '../desktop/.deps')
)

$ErrorActionPreference = 'Stop'

# Comprueba los archivos necesarios y la versión declarada por las fuentes.
function Assert-GoogleTestSource {
    param([string]$Root, [string]$ExpectedVersion)

    foreach ($relativePath in @(
        'CMakeLists.txt', 'LICENSE', 'googletest/CMakeLists.txt',
        'googletest/cmake/internal_utils.cmake', 'googletest/include/gtest/gtest.h',
        'googletest/src/gtest-all.cc', 'googletest/src/gtest_main.cc'
    )) {
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf) -or
            (Get-Item -LiteralPath $filePath).Length -eq 0) {
            throw "GoogleTest source is incomplete: missing or empty file $filePath"
        }
    }

    $cmakeText = Get-Content -LiteralPath (Join-Path $Root 'CMakeLists.txt') -Raw -Encoding UTF8
    $versionMatch = [regex]::Match($cmakeText, '(?m)^set\(GOOGLETEST_VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)\s*\)')
    if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -cne $ExpectedVersion) {
        throw "GoogleTest source version does not match the manifest: expected $ExpectedVersion. Root: $Root"
    }
}

# Localiza el manifiesto desde el script, independientemente de la terminal.
$manifestPath = Join-Path $PSScriptRoot '../desktop/googletest.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($fieldName in @('version', 'revision', 'archive', 'url', 'sha256')) {
    if ($manifest.$fieldName -isnot [string] -or [string]::IsNullOrWhiteSpace($manifest.$fieldName)) {
        throw "GoogleTest manifest requires a non-empty string field: $fieldName"
    }
}

if ($manifest.version -cnotmatch '^[0-9]+\.[0-9]+\.[0-9]+$' -or
    $manifest.revision -cnotmatch '^[0-9a-f]{40}$' -or
    $manifest.sha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw 'GoogleTest manifest has an invalid version, revision or SHA-256 format.'
}
if (($manifest.sizeBytes -isnot [int] -and $manifest.sizeBytes -isnot [long]) -or
    $manifest.sizeBytes -le 0) {
    throw 'GoogleTest manifest sizeBytes must be a positive integer.'
}

# Exige el archivo seleccionado y la URL HTTPS oficial de esa release.
$sourceDirectoryName = 'googletest-' + $manifest.version
if ($manifest.archive -cne ($sourceDirectoryName + '.tar.gz') -or
    $manifest.url -cne ("https://github.com/google/googletest/releases/download/v$($manifest.version)/$($manifest.archive)")) {
    throw 'GoogleTest manifest archive and URL must match the selected official release.'
}

# Conserva la descarga y las fuentes en carpetas separadas dentro de las dependencias.
$dependenciesRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DependenciesDirectory)
$archivePath = Join-Path (Join-Path $dependenciesRoot 'downloads') $manifest.archive
$googleTestDirectory = Join-Path $dependenciesRoot 'googletest'
$googleTestRoot = Join-Path $googleTestDirectory $sourceDirectoryName
Write-Output "GoogleTest version: $($manifest.version)"

# Reutiliza unas fuentes existentes solo si superan las comprobaciones esenciales.
if (Test-Path -LiteralPath $googleTestRoot) {
    Assert-GoogleTestSource -Root $googleTestRoot -ExpectedVersion $manifest.version
    Write-Output 'Existing GoogleTest sources passed essential file and version checks.'
    Write-Output "SBP_GOOGLETEST_ROOT: $googleTestRoot"
    return
}

$downloadRequired = -not (Test-Path -LiteralPath $archivePath)
$candidatePath = $archivePath
if ($downloadRequired) {
    $candidatePath = $archivePath + '.' + [guid]::NewGuid().ToString('N') + '.partial'
}

try {
    if ($downloadRequired) {
        New-Item -ItemType Directory -Path (Split-Path -Parent $archivePath) -Force | Out-Null
        $ProgressPreference = 'SilentlyContinue'
        Write-Output "Downloading GoogleTest from $($manifest.url)"
        Invoke-WebRequest -UseBasicParsing -Uri $manifest.url -OutFile $candidatePath
    }

    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        throw "GoogleTest archive is not a file: $candidatePath"
    }
    if ((Get-Item -LiteralPath $candidatePath).Length -ne $manifest.sizeBytes) {
        throw "GoogleTest archive size does not match the manifest. File: $candidatePath"
    }
    if ((Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash -ne $manifest.sha256) {
        throw "GoogleTest archive SHA-256 does not match the manifest. File: $candidatePath"
    }

    # Publica el nombre definitivo únicamente después de verificar el contenido.
    if ($downloadRequired) {
        Move-Item -LiteralPath $candidatePath -Destination $archivePath
    }
    Write-Output 'GoogleTest archive passed size and SHA-256 checks.'
} finally {
    # Retira únicamente el archivo parcial de este intento si todavía existe.
    if ($downloadRequired -and (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        Remove-Item -LiteralPath $candidatePath
    }
}

# tar.exe de Windows extrae el paquete verificado en una carpeta temporal única.
$extractorCommand = Get-Command tar.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1
$extractionDirectory = Join-Path $googleTestDirectory ('.extract-' + [guid]::NewGuid().ToString('N'))
$stagedRoot = Join-Path $extractionDirectory $sourceDirectoryName

# Verifica los destinos absolutos antes de mover carpetas o limpiar recursivamente.
$allowedPrefix = [System.IO.Path]::GetFullPath($googleTestDirectory).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
foreach ($operationPath in @($extractionDirectory, $stagedRoot, $googleTestRoot)) {
    if (-not [System.IO.Path]::GetFullPath($operationPath).StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "GoogleTest extraction path is outside the selected destination: $operationPath"
    }
}

try {
    New-Item -ItemType Directory -Path $extractionDirectory -Force | Out-Null
    & $extractorCommand.Source -xf $archivePath -C $extractionDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "GoogleTest extraction failed with exit code $LASTEXITCODE."
    }

    Assert-GoogleTestSource -Root $stagedRoot -ExpectedVersion $manifest.version
    Move-Item -LiteralPath $stagedRoot -Destination $googleTestRoot
    Write-Output 'GoogleTest sources extracted and checked.'
} finally {
    # La carpeta temporal comprobada pertenece exclusivamente a esta extracción.
    if (Test-Path -LiteralPath $extractionDirectory -PathType Container) {
        Remove-Item -LiteralPath $extractionDirectory -Recurse -Force
    }
}

Write-Output "SBP_GOOGLETEST_ROOT: $googleTestRoot"
