[CmdletBinding()]
param(
    [ValidateRange(1, 300)]
    [int]$TimeoutSeconds = 60
)

$ErrorActionPreference = 'Stop'
$desktopRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$buildRoot = [IO.Path]::GetFullPath((Join-Path $desktopRoot 'build'))
$directoryName = 'configuration-rejection-{0}' -f [Guid]::NewGuid().ToString('N')
$testDirectory = Join-Path $buildRoot $directoryName
$process = $null
$exitCode = 0
try {
    $cmakePath = (Get-Command cmake -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
    $null = [IO.Directory]::CreateDirectory($testDirectory)
    $outputPath = Join-Path $testDirectory 'cmake-stdout.log'
    $errorPath = Join-Path $testDirectory 'cmake-stderr.log'

    # La carpeta nueva evita heredar Qt6_DIR de la caché de una configuración correcta.
    # Usamos el generador y el toolset del proyecto; Qt6_DIR se vacía deliberadamente.
    $arguments = @('-S', ('"{0}"' -f $desktopRoot),
        '-B', ('"{0}"' -f (Join-Path $testDirectory 'build')),
        '-G', '"Visual Studio 17 2022"', '-A', 'x64', '-T', 'v143', '-DQt6_DIR:PATH=')
    $process = Start-Process -FilePath $cmakePath -ArgumentList $arguments `
        -WorkingDirectory $testDirectory -WindowStyle Hidden `
        -RedirectStandardOutput $outputPath -RedirectStandardError $errorPath -PassThru
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        throw "La configuración ha superado $TimeoutSeconds segundos."
    }

    # Un fallo distinto no acredita el rechazo esperado: exigimos el diagnóstico específico de Qt.
    $cmakeOutput = (Get-Content -LiteralPath $outputPath -Raw -Encoding UTF8) +
        (Get-Content -LiteralPath $errorPath -Raw -Encoding UTF8)
    if ($process.ExitCode -eq 0) {
        throw 'CMake ha aceptado una configuración sin Qt6_DIR.'
    }
    if ($cmakeOutput -notmatch 'Qt 6\.12\.0 MSVC 2022 x64 is required') {
        throw "CMake ha fallado por otro motivo; código: $($process.ExitCode). $cmakeOutput"
    }
    Write-Host "Configuración sin Qt rechazada correctamente; código de CMake: $($process.ExitCode)."
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $exitCode = 1
} finally {
    if ($null -ne $process) {
        try {
            if (-not $process.HasExited) {
                # El handle conserva la identidad del proceso; solo terminamos este intento y su árbol.
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                if ($LASTEXITCODE -ne 0 -or -not $process.WaitForExit(5000)) {
                    [Console]::Error.WriteLine('No se ha podido completar la limpieza del proceso CMake de prueba.')
                    $exitCode = 1
                }
            }
        } finally {
            $process.Dispose()
        }
    }

    # Comprobamos la ruta absoluta antes de borrar únicamente la carpeta creada por esta prueba.
    if (Test-Path -LiteralPath $testDirectory -PathType Container) {
        try {
            $resolvedTestDirectory = (Resolve-Path -LiteralPath $testDirectory).Path
            $buildPrefix = $buildRoot.TrimEnd([char[]]'\/') + [IO.Path]::DirectorySeparatorChar
            if (-not $resolvedTestDirectory.StartsWith($buildPrefix, [StringComparison]::OrdinalIgnoreCase) -or
                [IO.Path]::GetFileName($resolvedTestDirectory) -ne $directoryName) {
                throw 'La carpeta de limpieza no pertenece a esta prueba dentro de build.'
            }
            # Tras cancelar el árbol, algún archivo puede seguir bloqueado durante la finalización.
            # Reintentamos solo en esta carpeta durante un máximo de cinco segundos.
            $cleanupTimer = [Diagnostics.Stopwatch]::StartNew()
            while (Test-Path -LiteralPath $resolvedTestDirectory -PathType Container) {
                try {
                    Remove-Item -LiteralPath $resolvedTestDirectory -Recurse -Force
                } catch {
                    if ($cleanupTimer.ElapsedMilliseconds -ge 5000) {
                        throw
                    }
                    Start-Sleep -Milliseconds 200
                }
            }
            Write-Host 'Carpeta aislada de prueba eliminada.'
        } catch {
            [Console]::Error.WriteLine("Limpieza incompleta en $testDirectory. $($_.Exception.Message)")
            $exitCode = 1
        }
    }
}

exit $exitCode
