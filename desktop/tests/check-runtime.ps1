[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$InventoryPath,

    # Permite comprobar una copia aislada sin modificar el inventario generado por CMake.
    [string]$RuntimeDirectory
)

$ErrorActionPreference = 'Stop'
try {
    # Leemos el formato conocido antes de utilizar sus rutas como entradas de la comprobación.
    $inventory = Get-Content -LiteralPath $InventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($inventory.schema_version -ne 1) {
        throw 'El inventario no utiliza schema_version 1.'
    }
    if ($inventory.configuration -notin @('Debug', 'Release')) {
        throw 'El inventario debe corresponder a Debug o Release.'
    }
    if ($inventory.required_files -isnot [Array] -or $inventory.required_files.Count -eq 0) {
        throw 'El inventario debe contener una lista no vacía de archivos requeridos.'
    }

    # La carpeta normal procede de CMake; una prueba negativa puede entregar su propia copia.
    if ([string]::IsNullOrWhiteSpace($RuntimeDirectory)) {
        $RuntimeDirectory = $inventory.runtime_directory
    }
    if ([string]::IsNullOrWhiteSpace($RuntimeDirectory) -or
        -not (Test-Path -LiteralPath $RuntimeDirectory -PathType Container)) {
        throw "La carpeta de ejecución no existe: $RuntimeDirectory"
    }
    $runtimeRoot = (Resolve-Path -LiteralPath $RuntimeDirectory).Path
    $runtimePrefix = $runtimeRoot.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar

    $missingFiles = @()
    foreach ($relativeFile in $inventory.required_files) {
        # Cada entrada debe localizar un archivo dentro de la carpeta que estamos verificando.
        if ($relativeFile -isnot [string] -or [string]::IsNullOrWhiteSpace($relativeFile) -or
            [IO.Path]::IsPathRooted($relativeFile)) {
            throw 'El inventario contiene una entrada que no es una ruta relativa de archivo.'
        }
        $filePath = [IO.Path]::GetFullPath((Join-Path $runtimeRoot $relativeFile))
        if (-not $filePath.StartsWith($runtimePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "La entrada sale de la carpeta de ejecución: $relativeFile"
        }
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            $missingFiles += $relativeFile
        }
    }
    if ($missingFiles.Count -gt 0) {
        throw "Runtime $($inventory.configuration) incompleto. Faltan: $($missingFiles -join ', ')"
    }

    Write-Host "Runtime $($inventory.configuration) correcto: $($inventory.required_files.Count) archivos encontrados."
    exit 0
} catch {
    # El código distinto de cero permitirá integrar este comprobador en la batería y en CI.
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
