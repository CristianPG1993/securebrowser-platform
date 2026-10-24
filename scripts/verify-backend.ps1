# Ejecuta mvn verify con un PostgreSQL desechable, sin cargar el .env local.
$ErrorActionPreference = 'Stop'
$sbpVerifyRoot = Split-Path -Parent $PSScriptRoot
$sbpVerifyContainer = 'sbp-backend-check-' + [Guid]::NewGuid().ToString('N').Substring(0, 12)
$sbpVerifyStarted = $false
$sbpVerifyExit = 1
$sbpPreviousEnvironment = @{}
foreach ($sbpName in @('SBP_DB_URL', 'SBP_DB_USERNAME', 'SBP_DB_PASSWORD')) {
    $sbpPreviousEnvironment[$sbpName] = [Environment]::GetEnvironmentVariable($sbpName, 'Process')
}

Push-Location $sbpVerifyRoot
try {
    New-Item -ItemType Directory -Path backend/target -Force | Out-Null
    Write-Output 'Preparando PostgreSQL temporal para las pruebas del backend...'
    $ErrorActionPreference = 'Continue'
    # Credencial pública y desechable, exclusiva de esta base de pruebas.
    docker run --detach --rm --name $sbpVerifyContainer `
        -e POSTGRES_USER=sbp_app -e POSTGRES_PASSWORD=sbp_disposable_test_password `
        -e POSTGRES_DB=securebrowser_db -p 127.0.0.1::5432 postgres:17 `
        *> backend/target/verify-backend-postgres.log
    $sbpDockerExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if ($sbpDockerExit -ne 0) {
        throw 'No se pudo iniciar PostgreSQL. Comprueba Docker Desktop y backend/target/verify-backend-postgres.log.'
    }
    $sbpVerifyStarted = $true
    $sbpReady = $false
    for ($sbpAttempt = 0; $sbpAttempt -lt 60; $sbpAttempt++) {
        $ErrorActionPreference = 'Continue'
        docker exec $sbpVerifyContainer pg_isready -U sbp_app -d securebrowser_db *> $null
        $sbpReadyExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        if ($sbpReadyExit -eq 0) { $sbpReady = $true; break }
        Start-Sleep -Seconds 1
    }
    if (-not $sbpReady) { throw 'PostgreSQL temporal no estuvo listo a tiempo.' }
    $sbpPort = (docker port $sbpVerifyContainer 5432/tcp).Trim().Split(':')[-1]
    if ($LASTEXITCODE -ne 0 -or $sbpPort -notmatch '^\d+$') { throw 'No se pudo obtener el puerto de PostgreSQL.' }
    $env:SBP_DB_URL = "jdbc:postgresql://127.0.0.1:$sbpPort/securebrowser_db"
    $env:SBP_DB_USERNAME = 'sbp_app'
    $env:SBP_DB_PASSWORD = 'sbp_disposable_test_password'
    Write-Output 'Ejecutando mvn verify. El informe completo se guarda en backend/target/verify-backend.log.'
    $ErrorActionPreference = 'Continue'
    & .\backend\mvnw.cmd -f backend/pom.xml --batch-mode --no-transfer-progress verify `
        *> backend/target/verify-backend.log
    $sbpVerifyExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    Get-Content -Encoding UTF8 backend/target/verify-backend.log -Tail 28
} finally {
    $ErrorActionPreference = 'Continue'
    if ($sbpVerifyStarted) {
        docker stop $sbpVerifyContainer *> backend/target/verify-backend-cleanup.log
        if ($LASTEXITCODE -ne 0) {
            $sbpVerifyExit = 1
            Write-Output "No se pudo retirar $sbpVerifyContainer; consulta backend/target/verify-backend-cleanup.log."
        }
    }
    foreach ($sbpName in $sbpPreviousEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($sbpName, $sbpPreviousEnvironment[$sbpName], 'Process')
    }
    Pop-Location
}
exit $sbpVerifyExit
