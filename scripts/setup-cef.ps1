# Prepara el SDK (Software Development Kit) de CEF (Chromium Embedded Framework).
# $PSScriptRoot es la carpeta del script; .. sube a la raíz del repositorio.
[CmdletBinding()]
param(
    # Permite elegir otro destino; por defecto utiliza desktop/.deps.
    [ValidateNotNullOrEmpty()]
    [string]$DependenciesDirectory = (Join-Path $PSScriptRoot '../desktop/.deps')
)

# Detiene el script si un cmdlet encuentra un error.
$ErrorActionPreference = 'Stop'

# Comprueba los archivos esenciales, la versión y la arquitectura del SDK extraído.
function Assert-CefSdk {
    param([string]$Root, [string]$ExpectedVersion)

    # Archivos comunes: cabeceras, configuración CMake, recursos y avisos de licencia.
    $requiredFiles = @(
        'CMakeLists.txt', 'README.txt', 'LICENSE.txt', 'CREDITS.html',
        'include/cef_version.h', 'include/cef_app.h', 'include/cef_sandbox_win.h',
        'cmake/FindCEF.cmake', 'cmake/cef_variables.cmake', 'cmake/cef_macros.cmake',
        'libcef_dll/CMakeLists.txt', 'Resources/icudtl.dat',
        'Resources/chrome_100_percent.pak', 'Resources/chrome_200_percent.pak',
        'Resources/resources.pak', 'Resources/locales/en-US.pak'
    )
    # Cada configuración necesita sus propias bibliotecas y su bootstrap de arranque.
    foreach ($configuration in @('Debug', 'Release')) {
        foreach ($fileName in @(
            'bootstrap.exe', 'libcef.dll', 'libcef.lib', 'chrome_elf.dll',
            'v8_context_snapshot.bin', 'd3dcompiler_47.dll', 'dxcompiler.dll',
            'vk_swiftshader.dll', 'vk_swiftshader_icd.json', 'vulkan-1.dll'
        )) {
            $requiredFiles += "$configuration/$fileName"
        }
    }

    # Rechaza una preparación si falta un archivo esencial o está vacío.
    foreach ($relativePath in $requiredFiles) {
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf) -or
            (Get-Item -LiteralPath $filePath).Length -eq 0) {
            throw "CEF SDK is incomplete: missing or empty file $filePath"
        }
    }

    # Lee la versión declarada dentro del SDK y la compara con la del manifiesto.
    $versionHeader = Get-Content -LiteralPath (Join-Path $Root 'include/cef_version.h') -Raw -Encoding UTF8
    $versionMatch = [regex]::Match($versionHeader, '(?m)^#define\s+CEF_VERSION\s+"([^"]+)"')
    if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -cne $ExpectedVersion) {
        throw "CEF SDK version does not match the manifest: expected $ExpectedVersion. Root: $Root"
    }

    # Lee las cabeceras PE (Portable Executable) sin ejecutar los binarios.
    foreach ($configuration in @('Debug', 'Release')) {
        foreach ($binaryName in @('bootstrap.exe', 'libcef.dll')) {
            $binaryPath = Join-Path $Root "$configuration/$binaryName"
            $reader = [System.IO.BinaryReader]::new([System.IO.File]::OpenRead($binaryPath))
            try {
                # Un ejecutable Windows comienza con la firma MZ (0x5A4D).
                if ($reader.BaseStream.Length -lt 64 -or $reader.ReadUInt16() -ne 0x5A4D) {
                    throw "CEF binary is not a valid Windows executable: $binaryPath"
                }
                # En 0x3C se encuentra la posición de la cabecera PE dentro del archivo.
                $reader.BaseStream.Position = 0x3C
                $peOffset = $reader.ReadInt32()
                if ($peOffset -lt 64 -or $peOffset -gt ($reader.BaseStream.Length - 6)) {
                    throw "CEF binary has an invalid PE header offset: $binaryPath"
                }
                $reader.BaseStream.Position = $peOffset
                # Comprueba la firma PE y el identificador de arquitectura x64 (0x8664).
                if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) {
                    throw "CEF binary must be a Windows x64 executable: $binaryPath"
                }
            } finally {
                # Cierra el archivo aunque alguna comprobación produzca un error.
                $reader.Dispose()
            }
        }
    }
}

# Localiza el manifiesto desde el script, independientemente de la carpeta de la terminal.
$manifestPath = Join-Path $PSScriptRoot '../desktop/cef-sdk.json'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "CEF manifest not found: $manifestPath"
}

# Convierte el contenido JSON en un objeto cuyas propiedades podemos consultar.
try {
    $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
} catch {
    throw "Cannot read CEF manifest as JSON: $($_.Exception.Message)"
}

# Comprueba los campos de texto obligatorios antes de utilizarlos.
foreach ($fieldName in @('version', 'platform', 'distribution', 'archive', 'url', 'sha1')) {
    if ($manifest.$fieldName -isnot [string] -or [string]::IsNullOrWhiteSpace($manifest.$fieldName)) {
        throw "CEF manifest requires a non-empty string field: $fieldName"
    }
}

# Valida el formato de versión y exige la distribución Standard para Windows x64.
if ($manifest.version -cnotmatch '^\d+\.\d+\.\d+\+g[0-9a-f]+\+chromium-\d+\.\d+\.\d+\.\d+$') {
    throw 'CEF manifest has an invalid version format.'
}

if ($manifest.platform -cne 'windows64' -or $manifest.distribution -cne 'standard') {
    throw 'CEF manifest must select the Standard windows64 distribution.'
}

# El nombre del paquete y su carpeta raíz deben corresponder a la versión y plataforma.
$sdkDirectoryName = 'cef_binary_' + $manifest.version + '_' + $manifest.platform
if ($manifest.archive -cne ($sdkDirectoryName + '.tar.bz2')) {
    throw 'CEF archive name does not match the version and platform.'
}

# Aquí se validan los metadatos; el archivo real se comprobará después de descargarlo.
if (($manifest.sizeBytes -isnot [int] -and $manifest.sizeBytes -isnot [long]) -or $manifest.sizeBytes -le 0) {
    throw 'CEF manifest sizeBytes must be a positive integer.'
}

if ($manifest.sha1 -cnotmatch '^[0-9a-f]{40}$') {
    throw 'CEF manifest sha1 must contain 40 lowercase hexadecimal characters.'
}

# Exige una URL (Uniform Resource Locator) HTTPS del origen oficial y del archivo seleccionado.
$downloadUri = $null
if (-not [uri]::TryCreate($manifest.url, [UriKind]::Absolute, [ref]$downloadUri)) {
    throw 'CEF manifest URL must be absolute.'
}

if ($downloadUri.Scheme -ne 'https' -or $downloadUri.Host -ne 'cef-builds.spotifycdn.com' -or
    -not $downloadUri.IsDefaultPort -or $downloadUri.UserInfo -or $downloadUri.Query -or
    $downloadUri.Fragment -or [uri]::UnescapeDataString($downloadUri.AbsolutePath) -cne ('/' + $manifest.archive)) {
    throw 'CEF manifest URL must point to the selected archive on https://cef-builds.spotifycdn.com/.'
}

# Resuelve el destino absoluto sin crear carpetas y separa descarga y SDK extraído.
$dependenciesRoot = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DependenciesDirectory)
$archivePath = Join-Path (Join-Path $dependenciesRoot 'downloads') $manifest.archive
$cefRoot = Join-Path (Join-Path $dependenciesRoot 'cef') $sdkDirectoryName

Write-Output "CEF version: $($manifest.version)"
Write-Output "Archive path: $archivePath"
Write-Output "CEF_ROOT (planned): $cefRoot"

# Si el SDK ya existe y supera las comprobaciones, lo reutiliza y termina aquí.
if (Test-Path -LiteralPath $cefRoot) {
    Assert-CefSdk -Root $cefRoot -ExpectedVersion $manifest.version
    Write-Output 'Existing CEF SDK passed essential file, version and x64 checks; download and extraction skipped.'
    Write-Output "CEF_ROOT: $cefRoot"
    return
}

# Reutiliza el archivo comprimido existente o reserva un nombre temporal único .partial.
$downloadRequired = -not (Test-Path -LiteralPath $archivePath)
$candidatePath = $archivePath
if ($downloadRequired) {
    $candidatePath = $archivePath + '.' + [guid]::NewGuid().ToString('N') + '.partial'
}

try {
    if ($downloadRequired) {
        $downloadDirectory = Split-Path -Parent $archivePath
        New-Item -ItemType Directory -Path $downloadDirectory -Force | Out-Null
        $ProgressPreference = 'SilentlyContinue'
        Write-Output "Downloading CEF archive from $($manifest.url)"
        # La descarga todavía no utiliza el nombre definitivo del archivo.
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUri.AbsoluteUri -OutFile $candidatePath
    } else {
        Write-Output 'Checking cached CEF archive.'
    }

    if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        throw "CEF archive is not a file: $candidatePath"
    }

    # Comprueba primero el tamaño para detectar una descarga incompleta.
    $archiveSize = (Get-Item -LiteralPath $candidatePath).Length
    if ($archiveSize -ne $manifest.sizeBytes) {
        throw "CEF archive size mismatch: expected $($manifest.sizeBytes) bytes, got $archiveSize. File: $candidatePath"
    }

    # Calcula el SHA-1 (Secure Hash Algorithm 1) del contenido y lo compara con el publicado.
    $archiveHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA1).Hash
    if ($archiveHash -ne $manifest.sha1) {
        throw "CEF archive SHA-1 mismatch: expected $($manifest.sha1), got $archiveHash. File: $candidatePath"
    }

    # Solo da el nombre definitivo a una descarga que supera ambas comprobaciones.
    if ($downloadRequired) {
        Move-Item -LiteralPath $candidatePath -Destination $archivePath
        Write-Output 'Downloaded CEF archive passed size and SHA-1 verification.'
    } else {
        Write-Output 'Cached CEF archive verified and reused without downloading.'
    }
} finally {
    # Limpia únicamente el archivo temporal de este intento si todavía existe.
    if ($downloadRequired -and (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
        Remove-Item -LiteralPath $candidatePath
    }
}

# Utiliza tar de Windows para extraer el paquete completo en una carpeta temporal.
$tarCommand = Get-Command tar.exe -CommandType Application -ErrorAction Stop
$cefDirectory = Split-Path -Parent $cefRoot
$extractionDirectory = Join-Path $cefDirectory ('.extract-' + [guid]::NewGuid().ToString('N'))
$stagedCefRoot = Join-Path $extractionDirectory $sdkDirectoryName

# Limita el movimiento y la limpieza recursiva al directorio CEF elegido.
$allowedPrefix = [System.IO.Path]::GetFullPath($cefDirectory).TrimEnd([System.IO.Path]::DirectorySeparatorChar) + [System.IO.Path]::DirectorySeparatorChar
foreach ($operationPath in @($extractionDirectory, $stagedCefRoot, $cefRoot)) {
    if (-not [System.IO.Path]::GetFullPath($operationPath).StartsWith($allowedPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CEF extraction path is outside the selected destination: $operationPath"
    }
}

try {
    New-Item -ItemType Directory -Path $extractionDirectory -Force | Out-Null
    Write-Output 'Extracting the verified CEF archive into a temporary directory.'
    & $tarCommand.Source -xf $archivePath -C $extractionDirectory
    # Los programas externos requieren comprobar explícitamente su código de salida.
    if ($LASTEXITCODE -ne 0) {
        throw "CEF archive extraction failed with exit code $LASTEXITCODE."
    }
    # Valida el SDK antes de trasladarlo a la ubicación que utilizará el proyecto.
    Assert-CefSdk -Root $stagedCefRoot -ExpectedVersion $manifest.version
    Move-Item -LiteralPath $stagedCefRoot -Destination $cefRoot
    Write-Output 'CEF SDK extracted and checked: essential files, exact version and x64 binaries.'
} finally {
    # Limpia la carpeta temporal tanto al terminar correctamente como si hay un error.
    if (Test-Path -LiteralPath $extractionDirectory -PathType Container) {
        Remove-Item -LiteralPath $extractionDirectory -Recurse -Force
    }
}

# Muestra la ruta que proporcionaremos a CMake en la futura integración de CEF.
Write-Output "CEF_ROOT: $cefRoot"
