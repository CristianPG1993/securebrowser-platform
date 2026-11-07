[CmdletBinding()]
param(
    # Comprueba la instalación local sin descargar ni ejecutar el instalador.
    [switch]$CheckOnly,

    [ValidateNotNullOrEmpty()]
    [string]$DownloadDirectory = (Join-Path $PSScriptRoot '../.deps/windows-sdk')
)

$ErrorActionPreference = 'Stop'

# El nombre de la carpeta no distingue las revisiones del paquete; comprobamos también su registro.
function Test-WindowsSdkInstallation {
    param([string]$Root, [string]$PackageVersion, [string]$TargetVersion)

    $installedPackage = Get-ItemProperty -Path @(
        'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
        'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
    ) -ErrorAction SilentlyContinue | Where-Object {
        $_.DisplayName -eq "Windows Software Development Kit - Windows $PackageVersion"
    }
    if (-not $installedPackage) {
        return $false
    }

    # Son los componentes básicos para compilar, enlazar y generar recursos de una aplicación x64.
    foreach ($relativePath in @(
        "Include/$TargetVersion/um/Windows.h",
        "Include/$TargetVersion/shared/sdkddkver.h",
        "Include/$TargetVersion/ucrt/stdio.h",
        "Lib/$TargetVersion/um/x64/kernel32.lib",
        "Lib/$TargetVersion/ucrt/x64/ucrt.lib",
        "bin/$TargetVersion/x64/rc.exe",
        "bin/$TargetVersion/x64/mt.exe"
    )) {
        $filePath = Join-Path $Root $relativePath
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf) -or
            (Get-Item -LiteralPath $filePath).Length -eq 0) {
            return $false
        }
    }
    return $true
}

try {
    $toolchain = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'toolchain.json') -Raw -Encoding UTF8 |
        ConvertFrom-Json
    if ($toolchain.schemaVersion -ne 1 -or $toolchain.architecture -ne 'x64') {
        throw 'El manifiesto de CI debe utilizar schemaVersion 1 y arquitectura x64.'
    }
    $sdk = $toolchain.windowsSdk

    # El registro permite reutilizar también un SDK instalado fuera de su ubicación habitual.
    $kits = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots' -ErrorAction SilentlyContinue
    $sdkRoot = $kits.KitsRoot10
    if ([string]::IsNullOrWhiteSpace($sdkRoot)) {
        $sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10'
    }
    if (Test-WindowsSdkInstallation $sdkRoot $sdk.packageVersion $sdk.targetVersion) {
        Write-Host "Windows SDK $($sdk.packageVersion) ya está preparado: $sdkRoot"
        exit 0
    }
    if ($CheckOnly) {
        throw "Falta Windows SDK $($sdk.packageVersion) o alguno de sus componentes C++ x64."
    }

    # URL específica de la versión publicada en https://learn.microsoft.com/windows/apps/windows-sdk/downloads.
    $DownloadDirectory = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($DownloadDirectory)
    New-Item -ItemType Directory -Path $DownloadDirectory -Force | Out-Null
    $installerPath = Join-Path $DownloadDirectory "winsdksetup-$($sdk.packageVersion).exe"
    if (-not (Test-Path -LiteralPath $installerPath -PathType Leaf)) {
        Invoke-WebRequest -UseBasicParsing -Uri $sdk.installerUrl -OutFile $installerPath -TimeoutSec 120
    }
    if ($sdk.installerSha256 -notmatch '^[0-9a-fA-F]{64}$' -or
        (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash -ne $sdk.installerSha256) {
        throw "El SHA-256 del instalador no coincide con el manifiesto: $installerPath"
    }
    $signature = Get-AuthenticodeSignature -LiteralPath $installerPath
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch '(^|,\s*)O=Microsoft Corporation(,|$)') {
        throw 'El instalador no tiene una firma válida de Microsoft Corporation.'
    }

    # El instalador prepara únicamente C++ de escritorio x64, sin interfaz ni reinicio automático.
    Write-Host "Preparando Windows SDK $($sdk.packageVersion)."
    $installer = Start-Process -FilePath $installerPath -ArgumentList @(
        '/quiet', '/norestart', '/ceip', 'off', '/features', 'OptionId.DesktopCPPx64'
    ) -WindowStyle Hidden -Wait -PassThru
    try {
        if ($installer.ExitCode -notin @(0, 3010)) {
            throw "El instalador del Windows SDK terminó con código $($installer.ExitCode)."
        }
        if (-not (Test-WindowsSdkInstallation $sdkRoot $sdk.packageVersion $sdk.targetVersion)) {
            throw 'El instalador terminó, pero el SDK requerido no está completo.'
        }
        if ($installer.ExitCode -eq 3010) {
            Write-Warning 'El instalador ha indicado que hay un reinicio pendiente.'
        }
    } finally {
        $installer.Dispose()
    }
    Write-Host "Windows SDK $($sdk.packageVersion) preparado: $sdkRoot"
} catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}

exit 0
