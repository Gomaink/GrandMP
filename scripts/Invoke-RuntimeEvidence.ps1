[CmdletBinding(DefaultParameterSetName = 'Capture')]
param(
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$CheckOnly,
    [Parameter(ParameterSetName = 'Capture')]
    [switch]$LoaderLockOnly,
    [Parameter(Mandatory, ParameterSetName = 'Reprocess')]
    [string]$ReprocessDirectory
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

. (Join-Path $PSScriptRoot 'CdbHelpParser.ps1')
. (Join-Path $PSScriptRoot 'LoaderLockEvidence.ps1')

$script:ExpectedGameSha256 = 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'
$script:ExpectedAsiSha256 = '1EB0E9E22591402FA6002363DD0FC22B873B27F8224EA11C1FA3B356EEC7D2FA'
$script:ExpectedLoaderSha256 = '29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B'
$script:GameRoot = 'D:\Jogos\GrandMP-Smoke'
$script:RepoRoot = Split-Path -Parent $PSScriptRoot
$script:EvidenceRoot = Join-Path $script:RepoRoot 'build\runtime-evidence'

function Get-PeInfo {
    param([Parameter(Mandatory)][string]$Path)

    $stream = [System.IO.File]::OpenRead($Path)
    $reader = [System.IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Arquivo sem assinatura MZ: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadInt32()
        if ($peOffset -lt 0 -or $peOffset -gt ($stream.Length - 26)) { throw "Cabeçalho PE inválido: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { throw "Assinatura PE inválida: $Path" }
        $machine = $reader.ReadUInt16()
        $stream.Position = $peOffset + 24
        $optionalMagic = $reader.ReadUInt16()
        $imageBase = $null
        if ($optionalMagic -eq 0x10B) {
            $stream.Position = $peOffset + 24 + 28
            $imageBase = $reader.ReadUInt32()
        }
        return [pscustomobject]@{
            Machine = $machine
            OptionalMagic = $optionalMagic
            ImageBase = $imageBase
        }
    }
    finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

function Find-CdbX86 {
    $candidates = [System.Collections.Generic.List[string]]::new()

    # Prefer the x86 debugger installed with the Windows SDK. PATH may point to
    # the x64 debugger even when an x86 debugger is also installed.
    if (${env:ProgramFiles(x86)}) {
        $candidates.Add((Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\Debuggers\x86\cdb.exe'))
    }
    $candidates.Add('C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe')

    $kitsRoot = $null
    $registryPath = 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots'
    if (Test-Path -LiteralPath $registryPath) {
        $kitsRoot = (Get-ItemProperty -LiteralPath $registryPath -Name KitsRoot10 -ErrorAction SilentlyContinue).KitsRoot10
    }
    if (-not $kitsRoot -and ${env:ProgramFiles(x86)}) {
        $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\'
    }
    if ($kitsRoot) { $candidates.Add((Join-Path $kitsRoot 'Debuggers\x86\cdb.exe')) }
    $candidates.Add('C:\Program Files (x86)\Debugging Tools for Windows (x86)\cdb.exe')

    # Keep PATH as a fallback, after explicit x86 SDK locations.
    $command = Get-Command cdb.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates.Add($command.Source) }

    foreach ($candidate in ($candidates | Select-Object -Unique)) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $resolved = (Resolve-Path -LiteralPath $candidate).Path
            if ((Get-PeInfo -Path $resolved).Machine -eq 0x14C) { return $resolved }
        }
    }
    return $null
}

function Test-CdbParameters {
    param([Parameter(Mandatory)][string]$CdbPath)

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $CdbPath
    $startInfo.Arguments = '-?'
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(10000)) {
            $process.Kill()
            throw 'A consulta cdb.exe -? excedeu 10 segundos; nenhum alvo foi iniciado.'
        }
        # CDB versions can print usage to either stream. Read both concurrently
        # (above) to avoid pipe deadlocks, then validate the combined help text.
        $helpText = [string]::Concat($stdoutTask.Result, "`n", $stderrTask.Result)
        $parameters = Test-CdbHelpParameters -HelpText $helpText
        if (-not $parameters.Valid) {
            throw 'A ajuda local do CDB não confirmou os parâmetros -c <command>, -cf <file> e -p <pid>.'
        }
        return $helpText
    }
    finally { $process.Dispose() }
}

function ConvertTo-WindowsArgument {
    param([AllowEmptyString()][string]$Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append([string]::new([char]92, (2 * $slashes + 1)))
            [void]$builder.Append('"')
            $slashes = 0
            continue
        }
        if ($slashes -gt 0) { [void]$builder.Append([string]::new([char]92, $slashes)) }
        [void]$builder.Append($character)
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append([string]::new([char]92, (2 * $slashes))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-Cdb {
    param(
        [Parameter(Mandatory)][string]$CdbPath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$WorkingDirectory
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $CdbPath
    $startInfo.Arguments = (($Arguments | ForEach-Object { ConvertTo-WindowsArgument $_ }) -join ' ')
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $false
    $process = [System.Diagnostics.Process]::Start($startInfo)
    $script:DebuggerMayBeAttached = $true
    try {
        $process.WaitForExit()
        $script:DebuggerMayBeAttached = $false
        $script:LastCdbExitCode = $process.ExitCode
        if ($process.ExitCode -ne 0) { Write-Warning "CDB terminou com código $($process.ExitCode). Confira o log da etapa." }
    }
    finally { $process.Dispose() }
}

function Get-GameProcesses {
    $expectedPath = [System.IO.Path]::GetFullPath((Join-Path $script:GameRoot 'gta_sa.exe'))
    return @(Get-Process -Name gta_sa -ErrorAction SilentlyContinue | Where-Object {
        try { [string]::Equals([System.IO.Path]::GetFullPath($_.Path), $expectedPath, [System.StringComparison]::OrdinalIgnoreCase) }
        catch { $false }
    })
}

function Write-DebuggerScripts {
    param([Parameter(Mandatory)][string]$RunDirectory)

    if ($RunDirectory -match '\s') { throw "O caminho contém espaços, incompatíveis com o comando de arquivo usado nos breakpoints: $RunDirectory" }
    $initLog = Join-Path $RunDirectory '01-initialize-asi.txt'
    $menuLog = Join-Path $RunDirectory '02-menu-callsite.txt'
    $gameplayLog = Join-Path $RunDirectory '03-gameplay-callsite.txt'
    $initCapture = Join-Path $RunDirectory 'capture-initialize.wds'
    $initCaptureCdb = $initCapture.Replace('\', '\\')
    $menuCapture = Join-Path $RunDirectory 'capture-menu.wds'
    $gameplayCapture = Join-Path $RunDirectory 'capture-gameplay.wds'
    $initBoot = Join-Path $RunDirectory 'boot-initialize.wds'
    $menuBoot = Join-Path $RunDirectory 'boot-menu.wds'
    $gameplayBoot = Join-Path $RunDirectory 'boot-gameplay.wds'
    $moduleCaptureCdb = (Join-Path $RunDirectory 'capture-grandmp-load.wds').Replace('\', '\\')
    $pdbDirectory = Join-Path $script:RepoRoot 'build\alpha027-win32\client\windows\Debug'

    $initCaptureText = @"
.echo ===== InitializeASI: parada na entrada =====
|
~
r
kv 100
kb 100
dds @esp L80
lmv m GrandMP
lmv m dinput8
lmv m gta_sa
lmv m ntdll
lmv m kernel32
lmv m kernelbase
!ntsdexts.locks
.echo ===== fim da captura InitializeASI. detach =====
.logclose
.detach /h
q
"@
    $moduleCapture = Join-Path $RunDirectory 'capture-grandmp-load.wds'
    $moduleCaptureText = @"
.logopen $initLog
.echo ===== GrandMP load event. setting HW breakpoint on InitializeASI export =====
|
lmv m GrandMP
lmv m dinput8
lmv m gta_sa
.reload /f GrandMP
x GrandMP!InitializeASI
ba e 1 /1 GrandMP!InitializeASI "`$<$initCaptureCdb"
bl
g
"@
    $initBootText = @"
.symfix
.sympath+ $pdbDirectory
sxe -c "`$<$moduleCaptureCdb" ld:GrandMP
sxe -c "q" epr
.echo ===== waiting for GrandMP.asi load and InitializeASI export =====
g
"@

    $makeCapture = {
        param([string]$Stage, [string]$LogPath)
@"
.echo ===== call-site ${Stage}: antes do CALL =====
|
~.
~
r
kv 80
dd @esp L4
dds @esp L40
lmv m gta_sa
lmv m GrandMP
lmv m dinput8
db @`$t0 L5
? @`$t0+5
.echo ===== um passo no CALL. contexto na entrada CGame::Process =====
t
r
dd @esp L4
kv 80
dds @esp L40
.echo ===== fim da captura ${Stage}. detach =====
.logclose
.detach /h
q
"@
    }
    $menuCaptureText = & $makeCapture 'menu' $menuLog
    $gameplayCaptureText = & $makeCapture 'partida' $gameplayLog

    $makeBoot = {
        param([string]$Stage, [string]$LogPath, [string]$CapturePath)
        $capturePathCdb = $CapturePath.Replace('\', '\\')
@"
.symfix
.sympath+ $pdbDirectory
.reload /f GrandMP
.logopen $LogPath
sxe -c "q" epr
.echo ===== stage ${Stage}: validating bytes before arming breakpoint =====
|
lmv m gta_sa
r @`$t0 = gta_sa+0x13e981
r @`$t1 = gta_sa+0x13bee0
db @`$t0 L5
u @`$t0 L1
db @`$t1 L6
u @`$t1 L6
.if (((dwo(@`$t0) == 0xffd55ae8) & (by(@`$t0+4) == 0xff)) & ((dwo(@`$t1) == 0x530cec83) & (wo(@`$t1+4) == 0x5756))) { .echo VALIDATION_PASSED; ba e 1 /1 @`$t0 "`$<$capturePathCdb"; bl; g } .else { .echo FAIL_CLOSED }
.logclose
.detach /h
q
"@
    }
    $menuBootText = & $makeBoot 'menu' $menuLog $menuCapture
    $gameplayBootText = & $makeBoot 'partida' $gameplayLog $gameplayCapture

    $files = @{
        'capture-initialize.wds' = $initCaptureText
        'capture-grandmp-load.wds' = $moduleCaptureText
        'boot-initialize.wds' = $initBootText
        'capture-menu.wds' = $menuCaptureText
        'capture-gameplay.wds' = $gameplayCaptureText
        'boot-menu.wds' = $menuBootText
        'boot-gameplay.wds' = $gameplayBootText
    }
    foreach ($name in $files.Keys) {
        [System.IO.File]::WriteAllText((Join-Path $RunDirectory $name), $files[$name], [System.Text.Encoding]::ASCII)
    }
}

function Test-Installation {
    $gamePath = Join-Path $script:GameRoot 'gta_sa.exe'
    $asiPath = Join-Path $script:GameRoot 'scripts\GrandMP.asi'
    $loaderPath = Join-Path $script:GameRoot 'dinput8.dll'
    foreach ($required in @($gamePath, $asiPath, $loaderPath)) {
        if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Arquivo obrigatório ausente: $required" }
    }

    $gameHash = (Get-FileHash -LiteralPath $gamePath -Algorithm SHA256).Hash
    if ($gameHash -ne $script:ExpectedGameSha256) { throw "SHA-256 do GTA não homologado: $gameHash" }
    $asiHash = (Get-FileHash -LiteralPath $asiPath -Algorithm SHA256).Hash
    if ($asiHash -ne $script:ExpectedAsiSha256) { throw "SHA-256 do GrandMP.asi diferente do diagnóstico previamente validado: $asiHash" }
    $loaderHash = (Get-FileHash -LiteralPath $loaderPath -Algorithm SHA256).Hash
    if ($loaderHash -ne $script:ExpectedLoaderSha256) { throw "SHA-256 do loader diferente da versão documentada: $loaderHash" }

    $gamePe = Get-PeInfo -Path $gamePath
    if ($gamePe.Machine -ne 0x14C -or $gamePe.OptionalMagic -ne 0x10B) { throw 'gta_sa.exe não é PE32 x86.' }
    if ($gamePe.ImageBase -ne 0x00400000) { throw ('ImageBase PE inesperado: 0x{0:X8}' -f $gamePe.ImageBase) }

    $knownAsi = [System.IO.Path]::GetFullPath($asiPath)
    $searchDirs = @($script:GameRoot, (Join-Path $script:GameRoot 'scripts'), (Join-Path $script:GameRoot 'plugins'), (Join-Path $script:GameRoot 'update'))
    $foundAsis = foreach ($directory in $searchDirs) {
        if (Test-Path -LiteralPath $directory -PathType Container) {
            Get-ChildItem -LiteralPath $directory -Filter '*.asi' -File -ErrorAction SilentlyContinue
        }
    }
    $unexpectedAsis = @($foundAsis | Where-Object { -not [string]::Equals([System.IO.Path]::GetFullPath($_.FullName), $knownAsi, [System.StringComparison]::OrdinalIgnoreCase) })
    if ($unexpectedAsis.Count -gt 0) { throw ('ASI não revisado encontrado; desative-o manualmente antes: ' + (($unexpectedAsis.FullName) -join ', ')) }

    return [pscustomobject]@{ GamePath = $gamePath; GameHash = $gameHash; AsiHash = $asiHash; LoaderHash = $loaderHash }
}

try {
    if ($PSCmdlet.ParameterSetName -eq 'Reprocess') {
        $reprocessed = Export-LoaderLockReprocessedResult -CaptureDirectory $ReprocessDirectory -EvidenceRoot $script:EvidenceRoot
        Write-Host "LoaderLock: $($reprocessed.Result.Status). $($reprocessed.Result.Reason)"
        Write-Host "Novo resultado offline: $($reprocessed.Path)"
        Write-Host 'Log bruto e JSON original preservados. Nenhum debugger ou processo GTA foi consultado.'
        return
    }
    $script:DebuggerMayBeAttached = $false
    $cdb = Find-CdbX86
    if (-not $cdb) {
        $modern = Get-Command WinDbgX.exe -ErrorAction SilentlyContinue
        if ($modern) {
            throw "WinDbg moderno foi encontrado em '$($modern.Source)', mas não há CDB x86 instalado. Este fluxo usa CDB x86 para automatizar attach/detach entre menu e partida sem comandos digitados. Instale manualmente o componente Debugging Tools for Windows (x86) do Windows SDK e rode novamente."
        }
        throw 'CDB x86/WinDbg x86 não encontrado. Instale manualmente Debugging Tools for Windows (x86); nenhum instalador será executado pelo script.'
    }

    $cdbPe = Get-PeInfo -Path $cdb
    if ($cdbPe.Machine -ne 0x14C) { throw "O debugger encontrado não é x86: $cdb" }
    $help = Test-CdbParameters -CdbPath $cdb
    Write-Host "Debugger x86: $cdb"
    Write-Host 'Parâmetros locais confirmados pela saída de cdb.exe -?: -c <command>, -cf <file> e -p <pid>.'

    if ($CheckOnly) {
        Write-Host 'CheckOnly concluído. Nenhum processo GTA foi iniciado ou conectado.'
        return
    }

    $installation = Test-Installation
    $alreadyRunning = @(Get-Process -Name gta_sa -ErrorAction SilentlyContinue)
    if ($alreadyRunning.Count -gt 0) { throw 'Há um gta_sa.exe em execução. Feche todas as instâncias antes de começar; o launcher só abre uma sessão inequívoca.' }
    if (-not (Test-Path -LiteralPath (Join-Path $script:RepoRoot 'build\alpha027-win32\client\windows\Debug\GrandMP.pdb'))) {
        throw 'GrandMP.pdb da build com ASI correspondente não foi encontrado; não iniciar sem os símbolos locais registrados no roteiro.'
    }

    New-Item -ItemType Directory -Path $script:EvidenceRoot -Force | Out-Null
    $runId = Get-Date -Format 'yyyyMMdd-HHmmss'
    $runDirectory = Join-Path $script:EvidenceRoot $runId
    New-Item -ItemType Directory -Path $runDirectory | Out-Null
    if ($LoaderLockOnly) {
        $pdbDirectory = Join-Path $script:RepoRoot 'build\alpha027-win32\client\windows\Debug'
        Write-LoaderLockDebuggerScripts -RunDirectory $runDirectory -PdbDirectory $pdbDirectory
        $installation | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $runDirectory 'installation.json') -Encoding UTF8
    }
    else { Write-DebuggerScripts -RunDirectory $runDirectory }

    Write-Host "Jogo homologado: $($installation.GamePath)"
    Write-Host "Logs e scripts da execução: $runDirectory"
    Write-Host 'A captura usa .detach /h no evento de breakpoint identificado e encerra CDB com q, preservando o log.'
    Write-Host 'Não eleve o PowerShell. O Windowed Mode de terceiros deve permanecer desativado.'
    if ((Read-Host 'Digite SIM para iniciar a sessão de diagnóstico') -cne 'SIM') { throw 'Cancelado pelo usuário antes de iniciar o GTA.' }

    $initBoot = Join-Path $runDirectory 'boot-initialize.wds'
    Invoke-Cdb -CdbPath $cdb -Arguments @('-cf', $initBoot, $installation.GamePath) -WorkingDirectory $script:GameRoot

    if ($LoaderLockOnly) {
        $logPath = Join-Path $runDirectory '01-loader-lock.txt'
        $logText = if (Test-Path -LiteralPath $logPath) { [IO.File]::ReadAllText($logPath) } else { '' }
        $result = Get-LoaderLockEvidenceResult -LogText $logText
        if ($script:LastCdbExitCode -ne 0) {
            $result.Status = 'INCONCLUSIVO'
            $result.ReasonCode = 'CDB_EXIT_FAILURE'
            $result.Reason = "CDB encerrou com código $script:LastCdbExitCode; revisar o log antes de interpretar."
        }
        $result | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $runDirectory 'loader-lock-result.json') -Encoding UTF8
        Write-Host "LoaderLock: $($result.Status). $($result.Reason)"
        Write-Host "Evidência e relatório: $runDirectory"
        Write-Host 'A medição termina aqui. Encerre o GTA normalmente. Este resultado não autoriza integração com o motor.'
        return
    }

    $gameProcesses = @(Get-GameProcesses)
    if ($gameProcesses.Count -ne 1) { throw "Após a etapa InitializeASI, esperava uma instância GTA no diretório isolado; encontrei $($gameProcesses.Count). Verifique $runDirectory." }
    $gameProcess = $gameProcesses[0]

    Write-Host ''
    Write-Host 'Aguarde a janela principal chegar ao menu. Confirme que o jogo está visível no menu e então pressione Enter aqui.'
    [void](Read-Host 'Menu pronto')
    $menuProcess = @(Get-GameProcesses)
    if ($menuProcess.Count -ne 1 -or $menuProcess[0].Id -ne $gameProcess.Id) { throw 'O processo GTA homologado não está mais presente de forma inequívoca; attach cancelado.' }
    $menuBoot = Join-Path $runDirectory 'boot-menu.wds'
    Invoke-Cdb -CdbPath $cdb -Arguments @('-p', [string]$gameProcess.Id, '-cf', $menuBoot) -WorkingDirectory $script:GameRoot

    if (-not (Get-Process -Id $gameProcess.Id -ErrorAction SilentlyContinue)) { throw 'O GTA encerrou depois da captura do menu.' }
    Write-Host ''
    Write-Host 'Entre manualmente na partida. Quando CJ estiver visível e estável, pressione Enter aqui.'
    [void](Read-Host 'Partida pronta')
    $gameplayProcess = @(Get-GameProcesses)
    if ($gameplayProcess.Count -ne 1 -or $gameplayProcess[0].Id -ne $gameProcess.Id) { throw 'O processo GTA homologado não está mais presente de forma inequívoca; attach cancelado.' }
    $gameplayBoot = Join-Path $runDirectory 'boot-gameplay.wds'
    Invoke-Cdb -CdbPath $cdb -Arguments @('-p', [string]$gameProcess.Id, '-cf', $gameplayBoot) -WorkingDirectory $script:GameRoot

    Write-Host ''
    Write-Host "Capturas concluídas. Arquivos esperados em: $runDirectory"
    Write-Host 'O GTA deve continuar aberto e sem debugger conectado; encerre-o normalmente pelo menu.'
}
catch {
    Write-Host "ERRO: $_" -ForegroundColor Red
    if ($PSCmdlet.ParameterSetName -ne 'Reprocess' -and $script:DebuggerMayBeAttached) {
        Write-Host 'O CDB pode continuar conectado. Não use qd cegamente em uma parada STATUS_SINGLE_STEP; preserve o log/estado e use .detach /h somente no evento de breakpoint de hardware confirmado.'
    }
    exit 1
}
