# Prepara el SDK binario de ICU4C fijado en desktop/icu-sdk.json.
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$DependenciesDirectory = (Join-Path $PSScriptRoot '../desktop/.deps')
)

$ErrorActionPreference = 'Stop'

# Comprueba las cabeceras, bibliotecas de enlace y DLL que utilizará el núcleo.
function Assert-IcuSdk {
    param([string]$Root, [string]$ExpectedVersion, [string]$ExpectedUnicodeVersion)

    $majorVersion = $ExpectedVersion.Split('.')[0]
    foreach ($relativePath in @(
        'LICENSE', 'include/unicode/uvernum.h', 'include/unicode/uchar.h',
        'include/unicode/ustring.h', 'include/unicode/ucasemap.h',
        'include/unicode/utypes.h', 'include/unicode/utf8.h',
        'lib64/icuuc.lib', 'lib64/icudt.lib',
        "bin64/icuuc$majorVersion.dll", "bin64/icudt$majorVersion.dll"
    )) {
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf) -or
            (Get-Item -LiteralPath $filePath).Length -eq 0) {
            throw "ICU SDK is incomplete: missing or empty file $filePath"
        }
    }

    $versionHeader = Get-Content -LiteralPath (Join-Path $Root 'include/unicode/uvernum.h') -Raw -Encoding UTF8
    $versionMatch = [regex]::Match($versionHeader, '(?m)^#define[ \t]+U_ICU_VERSION[ \t]+"([^"]+)"')
    if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -cne $ExpectedVersion) {
        throw "ICU SDK version does not match the manifest: expected $ExpectedVersion. Root: $Root"
    }

    $unicodeHeader = Get-Content -LiteralPath (Join-Path $Root 'include/unicode/uchar.h') -Raw -Encoding UTF8
    $unicodeMatch = [regex]::Match($unicodeHeader, '(?m)^#define[ \t]+U_UNICODE_VERSION[ \t]+"([^"]+)"')
    if (-not $unicodeMatch.Success -or $unicodeMatch.Groups[1].Value -cne $ExpectedUnicodeVersion) {
        throw "ICU Unicode version does not match the manifest: expected $ExpectedUnicodeVersion. Root: $Root"
    }
}

# Lee y valida el manifiesto desde la ubicación del script.
$manifestPath = Join-Path $PSScriptRoot '../desktop/icu-sdk.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
foreach ($fieldName in @('version', 'unicodeVersion', 'revision', 'platform', 'toolset', 'distribution', 'archive', 'url', 'sha256')) {
    if ($manifest.$fieldName -isnot [string] -or [string]::IsNullOrWhiteSpace($manifest.$fieldName)) {
        throw "ICU manifest requires a non-empty string field: $fieldName"
    }
}

if ($manifest.version -cnotmatch '^[0-9]+\.[0-9]+$' -or
    $manifest.unicodeVersion -cnotmatch '^[0-9]+\.[0-9]+$' -or
    $manifest.revision -cnotmatch '^[0-9a-f]{40}$' -or
    $manifest.sha256 -cnotmatch '^[0-9a-f]{64}$') {
    throw 'ICU manifest has an invalid version, revision or SHA-256 format.'
}
if (($manifest.sizeBytes -isnot [int] -and $manifest.sizeBytes -isnot [long]) -or
    $manifest.sizeBytes -le 0) {
    throw 'ICU manifest sizeBytes must be a positive integer.'
}
if ($manifest.platform -cne 'windows64' -or $manifest.toolset -cne 'MSVC2022' -or
    $manifest.distribution -cne 'binary') {
    throw 'ICU manifest must select the Windows x64 MSVC 2022 binary distribution.'
}

# Exige el nombre y la URL oficiales correspondientes a la versión seleccionada.
$sdkDirectoryName = 'icu4c-' + $manifest.version.Replace('.', '_') + '-Win64-MSVC2022'
$releaseTag = 'release-' + $manifest.version.Replace('.', '-')
if ($manifest.archive -cne ($sdkDirectoryName + '.zip') -or
    $manifest.url -cne ("https://github.com/unicode-org/icu/releases/download/$releaseTag/$($manifest.archive)")) {
    throw 'ICU manifest archive and URL must match the selected official release.'
}

# Separa el paquete descargado de la copia preparada del SDK.
$dependenciesRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DependenciesDirectory)
$archivePath = Join-Path (Join-Path $dependenciesRoot 'downloads') $manifest.archive
$icuDirectory = Join-Path $dependenciesRoot 'icu'
$icuRoot = Join-Path $icuDirectory $sdkDirectoryName
Write-Output "ICU version: $($manifest.version); Unicode version: $($manifest.unicodeVersion)"

if (Test-Path -LiteralPath $icuRoot) {
    Assert-IcuSdk -Root $icuRoot -ExpectedVersion $manifest.version -ExpectedUnicodeVersion $manifest.unicodeVersion
    Write-Output 'Existing ICU SDK passed essential file and version checks.'
    Write-Output "SBP_ICU_ROOT: $icuRoot"
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
        Write-Output "Downloading ICU from $($manifest.url)"
        Invoke-WebRequest -UseBasicParsing -Uri $manifest.url -OutFile $candidatePath
    }

    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        throw "ICU archive is not a file: $candidatePath"
    }
    if ((Get-Item -LiteralPath $candidatePath).Length -ne $manifest.sizeBytes) {
        throw "ICU archive size does not match the manifest. File: $candidatePath"
    }
    if ((Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash -ne $manifest.sha256) {
        throw "ICU archive SHA-256 does not match the manifest. File: $candidatePath"
    }

    # Publica la descarga únicamente cuando se ha comprobado su integridad.
    if ($downloadRequired) {
        Move-Item -LiteralPath $candidatePath -Destination $archivePath
    }
    Write-Output 'ICU archive passed size and SHA-256 checks.'
} finally {
    # Retira solo el archivo parcial creado por este intento si todavía existe.
    if ($downloadRequired -and (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        Remove-Item -LiteralPath $candidatePath
    }
}

# El ZIP oficial contiene directamente include/, lib64/, bin64/ y LICENSE.
$extractorCommand = Get-Command tar.exe -CommandType Application -ErrorAction Stop | Select-Object -First 1
$extractionDirectory = Join-Path $icuDirectory ('.extract-' + [guid]::NewGuid().ToString('N'))

# Comprueba los destinos absolutos antes de mover carpetas o limpiar recursivamente.
$allowedPrefix = [IO.Path]::GetFullPath($icuDirectory).TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
foreach ($operationPath in @($extractionDirectory, $icuRoot)) {
    if (-not [IO.Path]::GetFullPath($operationPath).StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "ICU extraction path is outside the selected destination: $operationPath"
    }
}

try {
    New-Item -ItemType Directory -Path $extractionDirectory -Force | Out-Null
    & $extractorCommand.Source -xf $archivePath -C $extractionDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "ICU extraction failed with exit code $LASTEXITCODE."
    }

    Assert-IcuSdk -Root $extractionDirectory -ExpectedVersion $manifest.version -ExpectedUnicodeVersion $manifest.unicodeVersion
    # El movimiento falla si el destino ya existe, evitando mezclar dos copias del SDK.
    [IO.Directory]::Move($extractionDirectory, $icuRoot)
    Write-Output 'ICU SDK extracted and checked.'
} finally {
    # Limpia exclusivamente la carpeta temporal comprobada de esta extracción.
    if (Test-Path -LiteralPath $extractionDirectory -PathType Container) {
        Remove-Item -LiteralPath $extractionDirectory -Recurse -Force
    }
}

Write-Output "SBP_ICU_ROOT: $icuRoot"
