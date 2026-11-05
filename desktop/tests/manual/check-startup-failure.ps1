[CmdletBinding()]
param(
    [ValidateSet('ClientDll', 'CefResource')]
    [string]$Scenario = 'ClientDll',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug'
)

$ErrorActionPreference = 'Stop'
$desktopRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '../..')).Path
$sourceDirectory = (Resolve-Path -LiteralPath (Join-Path $desktopRoot "build/$Configuration")).Path
$missingFile = if ($Scenario -eq 'ClientDll') { 'SecureBrowser.dll' } else { 'icudtl.dat' }
foreach ($requiredFile in @('SecureBrowser.exe', $missingFile)) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceDirectory $requiredFile) -PathType Leaf)) {
        throw "Falta $requiredFile en la salida original. Compila antes de ejecutar esta prueba."
    }
}

# Cada prueba tiene una carpeta nueva dentro de build; no se altera la salida original.
$directoryName = 'startup-diagnostics-{0}-{1}' -f $Scenario, [Guid]::NewGuid().ToString('N')
$testDirectory = Join-Path (Join-Path $desktopRoot 'build') $directoryName
$null = New-Item -ItemType Directory -Path $testDirectory
foreach ($entry in Get-ChildItem -LiteralPath $sourceDirectory) {
    # Los símbolos, bibliotecas de enlace y logs antiguos no son necesarios para ejecutar la copia.
    if ($entry.PSIsContainer -or $entry.Extension -notin @('.pdb', '.lib', '.exp', '.log')) {
        Copy-Item -LiteralPath $entry.FullName -Destination $testDirectory -Recurse
    }
}

# Aplicamos a la copia los mismos permisos de lectura LPAC que configura el SDK CEF en CMake.
& icacls.exe $testDirectory /grant '*S-1-15-2-2:(OI)(CI)(RX)' | Out-Null
if ($LASTEXITCODE -ne 0) {
    throw 'No se han podido preparar los permisos LPAC de la copia.'
}
Rename-Item -LiteralPath (Join-Path $testDirectory $missingFile) -NewName "$missingFile.disabled"

Write-Host "Caso: $Scenario; configuración: $Configuration"
Write-Host "Copia aislada: $testDirectory"
Write-Host 'Si Windows o el bootstrap muestran un aviso, ciérralo para completar la prueba.'
$nativeErrorLog = Join-Path $testDirectory 'bootstrap-stderr.log'
$startedAt = [DateTime]::UtcNow
# Esta redirección recoge stderr anterior al log propio, si el cargador o bootstrap lo utilizan.
$process = Start-Process -FilePath (Join-Path $testDirectory 'SecureBrowser.exe') `
    -WorkingDirectory $testDirectory -RedirectStandardError $nativeErrorLog -PassThru
try {
    $null = $process.Handle
    $testProcessId = $process.Id
    # El timeout termina únicamente la instancia lanzada por esta prueba y sus posibles auxiliares.
    if (-not $process.WaitForExit(30000)) {
        & taskkill /PID $testProcessId /T /F | Out-Null
        throw 'La prueba ha superado 30 segundos; no se acredita una salida normal del error.'
    }
    Write-Host "Código de salida: $($process.ExitCode)"
} finally {
    $process.Dispose()
}

Write-Host "stderr del bootstrap, si hay contenido: $nativeErrorLog"
Get-Content -LiteralPath $nativeErrorLog
$logDirectory = Join-Path $env:LOCALAPPDATA 'SecureBrowser/Logs'
$applicationLog = $null
if (Test-Path -LiteralPath $logDirectory -PathType Container) {
    # Filtramos PID y fecha para no confundir este intento con un archivo de otra ejecución.
    $applicationLog = Get-ChildItem -LiteralPath $logDirectory -Filter "desktop-*-$testProcessId.log" |
        Where-Object LastWriteTimeUtc -ge $startedAt |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
}
if ($null -ne $applicationLog) {
    Write-Host "Log propio de esta ejecución: $($applicationLog.FullName)"
    Get-Content -LiteralPath $applicationLog.FullName -Tail 20
} else {
    Write-Host 'No hay log propio de este intento: revisar el aviso de Windows/bootstrap y su código.'
}

Write-Host 'La copia y sus diagnósticos se conservan dentro de build para revisarlos.'
