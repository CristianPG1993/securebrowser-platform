[CmdletBinding()]
param(
    [string]$InventoryPath = (Join-Path $PSScriptRoot '../build/runtime-inventory-Release.json'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot '../build/packages'),
    [string]$IsccPath
)

$ErrorActionPreference = 'Stop'
if (-not $IsccPath) {
    $onPath = Get-Command ISCC.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $candidates = @(
        $onPath.Source
        (Join-Path $env:LOCALAPPDATA 'Programs/Inno Setup 6/ISCC.exe')
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6/ISCC.exe')
        (Join-Path $env:ProgramFiles 'Inno Setup 6/ISCC.exe')
    )
    $IsccPath = $candidates | Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
}
if (-not $IsccPath -or -not (Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Instala Inno Setup 6 o indica la ruta de ISCC.exe con -IsccPath.'
}

# El empaquetador portátil valida el inventario Release y selecciona las dependencias.
$package = & "$PSScriptRoot/package-demo.ps1" -InventoryPath $InventoryPath -OutputDirectory $OutputDirectory -PassThru
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($package)) { throw 'Falló el empaquetado Release.' }
$manifest = Get-Content -LiteralPath (Join-Path $package 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.schema_version -ne 1 -or $manifest.configuration -ne 'Release' -or -not $manifest.files.Count) {
    throw 'Manifiesto de distribución Release inválido.'
}

# Antes de incorporar la carpeta completa, comprobamos que contiene exactamente los archivos registrados.
$entries = @{}
foreach ($entry in $manifest.files) { $entries.Add($entry.path, $entry) }
$required = (Get-Content -LiteralPath $InventoryPath -Raw -Encoding UTF8 | ConvertFrom-Json).required_files
foreach ($name in @($required) + @('SecureBrowser.exe', 'SecureBrowser.dll', 'Iniciar-demo.cmd', 'LEEME-demo.txt', 'vcruntime140.dll')) {
    if (-not $entries.ContainsKey($name.Replace('\', '/'))) { throw "Falta un archivo requerido: $name" }
}
$files = @(Get-ChildItem -LiteralPath $package -File -Recurse -Force)
if ($files.Count -ne $entries.Count + 1) { throw 'El número de archivos no coincide con el manifiesto.' }
foreach ($file in $files) {
    $relative = $file.FullName.Substring($package.Length + 1).Replace('\', '/')
    if ($relative -eq 'package-manifest.json') { continue }
    $entry = $entries[$relative]
    if (-not $entry -or $file.Length -ne $entry.bytes -or (Get-FileHash -LiteralPath $file.FullName).Hash -ne $entry.sha256) {
        throw "El archivo no coincide con el manifiesto: $relative"
    }
    if ($relative -match '(^|/)\.env($|\.)|\.(log|pdb|obj|lib|cpp|h|key|pem|pfx)$') {
        throw "Archivo ajeno a la distribución: $relative"
    }
}

# Una salida propia de esta pasada evita sobrescribir entregas anteriores o incluir el instalador en sí mismo.
$installerDir = $package + '-setup'
$null = New-Item -ItemType Directory -Path $installerDir
& $IsccPath /Qp "/DPackageDir=$package" "/DInstallerDir=$installerDir" "$PSScriptRoot/SecureBrowser.iss"
if ($LASTEXITCODE -ne 0) { throw 'Inno Setup no pudo compilar el instalador.' }
$installer = Join-Path $installerDir 'SecureBrowser-Setup.exe'
$hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash
"$hash  SecureBrowser-Setup.exe" | Set-Content -LiteralPath "$installer.sha256" -Encoding ASCII
Write-Host "Instalador: $installer"
Write-Host "SHA-256: $hash"
