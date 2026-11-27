[CmdletBinding()]
param(
    [string]$InventoryPath = (Join-Path $PSScriptRoot '../build/runtime-inventory-Release.json'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build/packages'),
    [string]$CompilerRuntimeDirectory,
    [switch]$PassThru
)

$ErrorActionPreference = 'Stop'
try {
    # Solo se distribuye Release; el inventario describe la salida real de CMake.
    $inventory = Get-Content -LiteralPath $InventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($inventory.schema_version -ne 1 -or $inventory.configuration -ne 'Release' -or
        $inventory.required_files -isnot [Array] -or $inventory.required_files.Count -eq 0) {
        throw 'Se requiere un inventario Release válido de schema_version 1.'
    }
    $runtimeRoot = (Resolve-Path -LiteralPath $inventory.runtime_directory).Path
    $runtimePrefix = $runtimeRoot.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
    $relativeFiles = @($inventory.required_files) + @('README.txt', 'LICENSE.txt', 'CREDITS.html')

    # Conserva las DLL y los plugins ya desplegados por windeployqt, sin copiar herramientas ni logs.
    foreach ($file in Get-ChildItem -LiteralPath $runtimeRoot -File -Filter '*.dll') {
        $relativeFiles += $file.Name
    }
    foreach ($directory in @('generic', 'iconengines', 'imageformats', 'networkinformation',
        'platforms', 'styles', 'tls', 'translations', 'qt-notices')) {
        $directoryPath = Join-Path $runtimeRoot $directory
        if ($directory -eq 'qt-notices' -and -not (Test-Path -LiteralPath $directoryPath -PathType Container)) {
            throw 'Faltan los avisos de Qt en la salida Release.'
        }
        if (Test-Path -LiteralPath $directoryPath -PathType Container) {
            foreach ($file in Get-ChildItem -LiteralPath $directoryPath -File -Recurse) {
                $relativeFiles += $file.FullName.Substring($runtimePrefix.Length)
            }
        }
    }
    $relativeFiles = @($relativeFiles | Sort-Object -Unique)

    # Valida todas las entradas antes de crear la carpeta del paquete.
    foreach ($relativeFile in $relativeFiles) {
        if ($relativeFile -isnot [string] -or [string]::IsNullOrWhiteSpace($relativeFile) -or
            [IO.Path]::IsPathRooted($relativeFile)) {
            throw 'El inventario contiene una ruta de archivo no relativa.'
        }
        $sourcePath = [IO.Path]::GetFullPath((Join-Path $runtimeRoot $relativeFile))
        if (-not $sourcePath.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
            throw "Archivo de ejecución ausente o fuera del runtime: $relativeFile"
        }
    }
    foreach ($fileName in @('Iniciar-demo.cmd', 'LEEME-demo.txt')) {
        if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot $fileName) -PathType Leaf)) {
            throw "Falta el archivo de distribución: $fileName"
        }
    }

    # Localiza las DLL redistribuibles x64 de VS 2022, sin utilizar las instaladas en System32.
    if ([string]::IsNullOrWhiteSpace($CompilerRuntimeDirectory)) {
        $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
        if (-not (Test-Path -LiteralPath $vswherePath -PathType Leaf)) {
            throw 'No se encuentra vswhere. Indica -CompilerRuntimeDirectory con la carpeta x64/Microsoft.VC143.CRT.'
        }
        $installationPath = & $vswherePath -latest -products '*' -version '[17.0,18.0)' `
            -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installationPath)) {
            throw 'No se encuentra una instalación de Visual Studio 2022 con herramientas C++ x64.'
        }
        $redistRoot = Join-Path $installationPath 'VC/Redist/MSVC'
        $versions = @(Get-ChildItem -LiteralPath $redistRoot -Directory |
            Where-Object { $_.Name -match '^14\.\d+\.\d+$' } |
            Sort-Object { [version]$_.Name } -Descending)
        if ($versions.Count -eq 0) {
            throw "No se encuentran versiones redistribuibles en $redistRoot."
        }
        $CompilerRuntimeDirectory = Join-Path $versions[0].FullName 'x64/Microsoft.VC143.CRT'
    }
    $compilerRoot = (Resolve-Path -LiteralPath $CompilerRuntimeDirectory).Path
    if ($compilerRoot -notmatch '[\\/]x64[\\/]Microsoft\.VC143\.CRT$') {
        throw 'La carpeta del compilador debe ser x64/Microsoft.VC143.CRT de los redistribuibles de VS 2022.'
    }
    foreach ($fileName in @('msvcp140.dll', 'msvcp140_1.dll', 'msvcp140_2.dll',
        'vcruntime140.dll', 'vcruntime140_1.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $compilerRoot $fileName) -PathType Leaf)) {
            throw "Falta la biblioteca redistribuible: $fileName"
        }
    }
    $compilerFiles = @(Get-ChildItem -LiteralPath $compilerRoot -File -Filter '*.dll')
    foreach ($file in $compilerFiles) {
        if ($relativeFiles -contains $file.Name) {
            throw "Ya existe una copia de $($file.Name) en Release; revisar su origen antes de empaquetar."
        }
    }

    # Cada pasada crea una carpeta nueva; no elimina ni sobrescribe paquetes anteriores.
    $packageName = 'SecureBrowser-demo-Release-' + (Get-Date -Format 'yyyyMMdd-HHmmss') +
        '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $packageRoot = [IO.Path]::GetFullPath((Join-Path $OutputDirectory $packageName))
    if ($packageRoot.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase) -or
        (Test-Path -LiteralPath $packageRoot)) {
        throw 'El paquete debe crearse en una carpeta nueva fuera del runtime original.'
    }
    $null = New-Item -ItemType Directory -Path $packageRoot
    foreach ($relativeFile in $relativeFiles) {
        $destinationPath = Join-Path $packageRoot $relativeFile
        $null = New-Item -ItemType Directory -Path (Split-Path -Parent $destinationPath) -Force
        Copy-Item -LiteralPath (Join-Path $runtimeRoot $relativeFile) -Destination $destinationPath
    }
    foreach ($file in $compilerFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $packageRoot $file.Name)
    }
    foreach ($fileName in @('Iniciar-demo.cmd', 'LEEME-demo.txt')) {
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot $fileName) -Destination (Join-Path $packageRoot $fileName)
    }

    # Conserva el permiso de lectura y ejecución LPAC que aplica el SDK CEF al runtime original.
    & icacls.exe $packageRoot /grant '*S-1-15-2-2:(OI)(CI)(RX)' | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw 'No se pudieron preparar los permisos de lectura y ejecución de CEF.'
    }

    # Los hashes describen exactamente los archivos preparados; no acreditan por sí solos sus pruebas.
    $packagePrefix = $packageRoot + [IO.Path]::DirectorySeparatorChar
    $manifestFiles = @(foreach ($file in Get-ChildItem -LiteralPath $packageRoot -File -Recurse | Sort-Object FullName) {
        [ordered]@{
            path = $file.FullName.Substring($packagePrefix.Length).Replace('\', '/')
            bytes = $file.Length
            sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
        }
    })
    $manifest = [ordered]@{
        schema_version = 1
        configuration = 'Release'
        created_at_utc = [DateTime]::UtcNow.ToString('o')
        compiler_runtime_version = (Get-Item -LiteralPath (Join-Path $compilerRoot 'vcruntime140.dll')).VersionInfo.FileVersion
        files = $manifestFiles
    }
    $manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $packageRoot 'package-manifest.json') -Encoding UTF8
    # El ZIP contiene la carpeta completa; el lanzador prepara los permisos de CEF al extraerlo.
    # Sus archivos auxiliares quedan fuera de la carpeta para evitar incluir el archivo dentro de sí mismo.
    $archivePath = $packageRoot + '.zip'
    Compress-Archive -LiteralPath $packageRoot -DestinationPath $archivePath -CompressionLevel Optimal
    $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash
    ($archiveHash + '  ' + [IO.Path]::GetFileName($archivePath)) |
        Set-Content -LiteralPath ($archivePath + '.sha256') -Encoding ASCII
    Write-Host "Carpeta preparada: $packageRoot"
    Write-Host "ZIP preparado: $archivePath"
    Write-Host "SHA-256 del ZIP: $archiveHash"
    Write-Host "Archivos registrados: $($manifestFiles.Count). Pendiente de comprobación manual antes de distribuir."
    if ($PassThru) { Write-Output $packageRoot }
    exit 0
} catch {
    # Si falla una copia, su carpeta parcial se conserva para diagnóstico y no se considera entregable.
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
