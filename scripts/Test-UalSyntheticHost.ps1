[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$LoaderPath,
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build\alpha035-win32\client\windows\Debug')
)

$ErrorActionPreference = 'Stop'
$expectedSha256 = '29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B'
$resolvedLoader = (Resolve-Path -LiteralPath $LoaderPath).Path
$actualSha256 = (Get-FileHash -LiteralPath $resolvedLoader -Algorithm SHA256).Hash
if ($actualSha256 -ne $expectedSha256) {
    throw "Loader SHA-256 não corresponde ao Ultimate ASI Loader Win32 7.7.0 verificado. Atual: $actualSha256"
}
Write-Warning 'O subprocesso usa o loader real com as permissões deste usuário; pasta temporária não é sandbox. Execute somente o binário verificado e em ambiente controlado.'

$hostPath = Join-Path $BuildDirectory 'grandmp_asi_loader_probe_host.exe'
$sentinelPath = Join-Path $BuildDirectory 'GrandMPUalProbe.asi'
foreach ($required in @($hostPath, $sentinelPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Artefato ausente: $required" }
}

$workspaceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$evidenceRoot = Join-Path $workspaceRoot 'build\runtime-evidence'
New-Item -ItemType Directory -Force -Path $evidenceRoot | Out-Null
$runName = 'ual-synthetic-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff')
$runDirectory = Join-Path $evidenceRoot $runName
New-Item -ItemType Directory -Path $runDirectory | Out-Null
Copy-Item -LiteralPath $resolvedLoader -Destination (Join-Path $runDirectory 'dinput8.dll')
Copy-Item -LiteralPath $hostPath -Destination (Join-Path $runDirectory 'probe-host.exe')
Copy-Item -LiteralPath $sentinelPath -Destination (Join-Path $runDirectory 'GrandMPUalProbe.asi')
Set-Content -LiteralPath (Join-Path $runDirectory 'dinput8.ini') -Encoding Ascii -Value @(
    '[GlobalSets]'
    'DontLoadFromDllMain=1'
    'LoadPlugins=1'
)

$stdoutPath = Join-Path $runDirectory 'stdout.txt'
$stderrPath = Join-Path $runDirectory 'stderr.txt'
$process = Start-Process -FilePath (Join-Path $runDirectory 'probe-host.exe') `
    -WorkingDirectory $runDirectory -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
if (-not $process.WaitForExit(15000)) {
    $process.Kill($true)
    throw "Host sintético excedeu 15 s; processo terminado. Evidências: $runDirectory"
}
$process.Refresh()
if ($process.ExitCode -ne 0) {
    throw "Host sintético terminou com exit code $($process.ExitCode). Evidências: $runDirectory"
}

$markerPath = Join-Path $runDirectory 'ual_probe_initialize_asi.marker'
if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf)) {
    throw "InitializeASI do sentinela não foi observado. Evidências: $runDirectory"
}
Write-Output "Ultimate ASI Loader 7.7.0 sintético: PASS; SHA-256=$actualSha256"
Write-Output "Saída: $((Get-Content -LiteralPath $stdoutPath -Raw).Trim())"
Write-Output "Sentinela: $((Get-Content -LiteralPath $markerPath -Raw).Trim())"
Write-Output "Evidências: $runDirectory"
