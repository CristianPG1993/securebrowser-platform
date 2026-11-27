[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [ValidateRange(1, 120)]
    [int]$StartupTimeoutSeconds = 30,

    [ValidateRange(1, 120)]
    [int]$CloseTimeoutSeconds = 30
)

$ErrorActionPreference = 'Stop'
$process = $null
$applicationLog = $null
$exitCode = 0
try {
    # Debug y Release comparten el perfil CEF; la prueba requiere una ejecución independiente.
    if (Get-Process -Name SecureBrowser -ErrorAction SilentlyContinue) {
        throw 'Cierra las instancias de SecureBrowser antes de ejecutar esta comprobación.'
    }
    $desktopRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
    $runtimeDirectory = (Resolve-Path -LiteralPath (Join-Path $desktopRoot "build/$Configuration")).Path
    $executablePath = Join-Path $runtimeDirectory 'SecureBrowser.exe'
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Falta SecureBrowser.exe en la salida $Configuration. Compila antes de ejecutar esta prueba."
    }
    # Consultamos las ventanas hijas sin leer el log, que la CRT mantiene abierto en modo exclusivo.
    if (-not ('Sbp.StartupWindowProbe' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;

namespace Sbp {
    public static class StartupWindowProbe {
        private delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool EnumChildWindows(IntPtr parent, EnumWindowProc callback, IntPtr parameter);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetClassName(IntPtr window, StringBuilder className, int capacity);

        [DllImport("user32.dll")]
        private static extern IntPtr GetWindow(IntPtr window, uint command);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);

        [DllImport("user32.dll")]
        private static extern bool IsWindowVisible(IntPtr window);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowText(IntPtr window, StringBuilder title, int capacity);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool PostMessageW(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

        public static bool TryCloseStartupNotice(IntPtr parent, int processId) {
            const uint GW_ENABLEDPOPUP = 6;
            const uint WM_CLOSE = 0x0010;
            IntPtr popup = GetWindow(parent, GW_ENABLEDPOPUP);
            if (popup == IntPtr.Zero || popup == parent || !IsWindowVisible(popup)) return false;

            uint ownerProcessId;
            GetWindowThreadProcessId(popup, out ownerProcessId);
            var title = new StringBuilder(128);
            GetWindowText(popup, title, title.Capacity);
            if (ownerProcessId != processId || title.ToString() != "Bloqueo de seguridad") return false;

            // Equivale a cerrar con la X el aviso de esta ventana. Qt vuelve a habilitar su propietaria.
            if (!PostMessageW(popup, WM_CLOSE, IntPtr.Zero, IntPtr.Zero)) {
                throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            }
            return true;
        }

        public static bool HasBrowserWindow(IntPtr parent) {
            bool found = false;
            // Esta lambda solo consulta las ventanas descendientes de la instancia que estamos probando.
            EnumChildWindows(parent, (window, parameter) => {
                var className = new StringBuilder(256);
                GetClassName(window, className, className.Capacity);
                // El runtime Alloy del SDK CEF fijado utiliza esta clase para su ventana hija.
                if (className.ToString() == "CefBrowserWindow") {
                    found = true;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            return found;
        }
    }
}
'@
    }
    $logDirectory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'SecureBrowser/Logs'
    $startedAt = [DateTime]::UtcNow

    # El proceso de prueba arranca sin backend. La configuración local del usuario sigue en su terminal.
    $startInfo = [Diagnostics.ProcessStartInfo]::new($executablePath)
    $startInfo.WorkingDirectory = $runtimeDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.EnvironmentVariables.Remove('SBP_BACKEND_CONFIG')
    $process = [Diagnostics.Process]::Start($startInfo)
    $null = $process.Handle
    $testProcessId = $process.Id
    Write-Host "Comprobando $Configuration; PID: $testProcessId."

    # Esperamos la ventana hija nativa, sin depender de la red ni acreditar su contenido visual.
    $startupTimer = [Diagnostics.Stopwatch]::StartNew()
    $browserWindowReady = $false
    $noticeCloseSent = $false
    while ($startupTimer.Elapsed.TotalSeconds -lt $StartupTimeoutSeconds) {
        $process.Refresh()
        if ($process.HasExited) {
            throw "La aplicación terminó antes de completar el arranque; código: $($process.ExitCode)."
        }
        $window = $process.MainWindowHandle
        if (-not $browserWindowReady -and $window -ne [IntPtr]::Zero -and
            [Sbp.StartupWindowProbe]::HasBrowserWindow($window)) {
            $browserWindowReady = $true
            Write-Host 'Ventana hija CEF detectada.'
        }
        if ($browserWindowReady -and $window -ne [IntPtr]::Zero -and
            [Sbp.StartupWindowProbe]::TryCloseStartupNotice($window, $testProcessId)) {
            $noticeCloseSent = $true
            break
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $browserWindowReady) {
        throw "El arranque ha superado $StartupTimeoutSeconds segundos."
    }
    if (-not $noticeCloseSent) {
        throw 'No se encontró el aviso de bloqueo de Inicio esperado en el arranque sin backend.'
    }
    Write-Host 'Cierre solicitado al aviso de bloqueo. Esperando el cierre normal de la ventana principal.'

    # Hasta que Qt procese el cierre del diálogo, CloseMainWindow devuelve false sin enviar la solicitud.
    $closeTimer = [Diagnostics.Stopwatch]::StartNew()
    while (-not $process.CloseMainWindow()) {
        if ($process.HasExited -or $closeTimer.Elapsed.TotalSeconds -ge $CloseTimeoutSeconds) {
            throw 'No se ha podido enviar el cierre a la ventana principal tras cerrar el aviso.'
        }
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    $remainingMs = [Math]::Max(0, $CloseTimeoutSeconds * 1000 - $closeTimer.ElapsedMilliseconds)
    if (-not $process.WaitForExit([int]$remainingMs)) {
        throw "El cierre ha superado $CloseTimeoutSeconds segundos."
    }
    if ($process.ExitCode -ne 0) {
        throw "La aplicación ha terminado con código $($process.ExitCode)."
    }

    # El proceso ya ha terminado y ha liberado el log; PID y fecha identifican esta ejecución.
    $applicationLog = Get-ChildItem -LiteralPath $logDirectory -Filter "desktop-*-$testProcessId.log" |
        Where-Object LastWriteTimeUtc -ge $startedAt |
        Sort-Object LastWriteTimeUtc -Descending |
        Select-Object -First 1
    if ($null -eq $applicationLog) {
        throw 'No se ha encontrado el diagnóstico de esta ejecución.'
    }
    $logContent = Get-Content -LiteralPath $applicationLog.FullName -Raw -Encoding UTF8
    $creation = [regex]::Match([string]$logContent, 'CEF browser created: id ([1-9][0-9]*)\.')
    if (-not $creation.Success) {
        throw 'El log no confirma la creación del navegador CEF.'
    }
    $browserId = $creation.Groups[1].Value
    # Exigimos las marcas en orden y el mismo identificador de navegador al crear y cerrar.
    $expectedMessages = @(
        'CEF initialized successfully with bootstrap sandbox context.',
        "CEF browser created: id $browserId.",
        "CEF browser closing: id $browserId.",
        'Qt window destroyed; event loop exit code 0.',
        'CEF shutdown completed.'
    )
    $nextIndex = 0
    foreach ($message in $expectedMessages) {
        $messageIndex = $logContent.IndexOf($message, $nextIndex, [StringComparison]::Ordinal)
        if ($messageIndex -lt 0) {
            throw "Falta una marca del ciclo de vida o está fuera de orden: $message"
        }
        $nextIndex = $messageIndex + $message.Length
    }
    Write-Host "Arranque y cierre $Configuration correctos; código de salida: 0."
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    $exitCode = 1
} finally {
    if ($null -ne $applicationLog) {
        Write-Host "Diagnóstico: $($applicationLog.FullName)"
    }
    if ($null -ne $process) {
        try {
            if (-not $process.HasExited) {
                # El handle sigue vivo: el PID corresponde a nuestra instancia. Solo se termina su árbol.
                & taskkill.exe /PID $process.Id /T /F | Out-Null
                if ($LASTEXITCODE -ne 0 -or -not $process.WaitForExit(5000)) {
                    [Console]::Error.WriteLine('No se ha podido completar la limpieza de la instancia de prueba.')
                    $exitCode = 1
                }
            }
        } finally {
            $process.Dispose()
        }
    }
}

exit $exitCode
