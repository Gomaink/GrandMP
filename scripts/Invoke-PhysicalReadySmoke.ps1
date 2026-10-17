[CmdletBinding()]
param([switch]$CheckOnly, [ValidateRange(1,300)][int]$ReadyTimeoutSeconds = 30,
    [ValidateRange(1,600)][int]$ExitTimeoutSeconds = 120)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Write-ReadySmokeJson($Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 9), [Text.UTF8Encoding]::new($false))
}
function ConvertTo-ReadySmokeUtc([object]$Value) {
    if ($Value -is [DateTimeOffset]) { return $Value.UtcDateTime }
    if ($Value -is [DateTime]) {
        if ($Value.Kind -eq [DateTimeKind]::Unspecified) { throw 'TIMESTAMP_UNSPECIFIED' }
        return $Value.ToUniversalTime()
    }
    if ($Value -is [string] -and $Value -match '(Z|[+-]\d{2}:\d{2})$') {
        return [DateTimeOffset]::Parse($Value, [Globalization.CultureInfo]::InvariantCulture).UtcDateTime
    }
    throw 'TIMESTAMP_INVALID'
}
function Get-ReadySmokeProcessSnapshot($Process) {
    if (-not ('ReadySmokeIdentityNative' -as [type])) {
        Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class ReadySmokeIdentityNative {
 [DllImport("kernel32.dll", SetLastError=true)] public static extern uint GetProcessId(IntPtr h);
 [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] public static extern bool QueryFullProcessImageName(IntPtr h,uint flags,StringBuilder path,ref uint length);
 [DllImport("kernel32.dll", SetLastError=true)] public static extern bool GetProcessTimes(IntPtr h,out long creation,out long exit,out long kernel,out long user);
}
'@
    }
    $path = [Text.StringBuilder]::new(32768); [uint32]$length = 32768
    [long]$creation=0; [long]$exit=0; [long]$kernel=0; [long]$user=0
    if (![ReadySmokeIdentityNative]::QueryFullProcessImageName($Process.Handle,0,$path,[ref]$length) -or
        ![ReadySmokeIdentityNative]::GetProcessTimes($Process.Handle,[ref]$creation,[ref]$exit,[ref]$kernel,[ref]$user)) { throw 'PROCESS_IDENTITY_QUERY_FAILED' }
    return [pscustomobject]@{Id=[ReadySmokeIdentityNative]::GetProcessId($Process.Handle);Path=$path.ToString();StartTime=[DateTime]::FromFileTimeUtc($creation)}
}
function Assert-ReadySmokeIdentity($ExpectedPID, $ExpectedPath, $ExpectedStart, $ObservedProcess) {
    if ($ObservedProcess.Id -ne $ExpectedPID) { throw 'PID_MISMATCH' }
    if (![string]::Equals([IO.Path]::GetFullPath($ObservedProcess.Path),
        [IO.Path]::GetFullPath($ExpectedPath), [StringComparison]::OrdinalIgnoreCase)) { throw 'PROCESS_PATH_MISMATCH' }
    $expectedUtc = ConvertTo-ReadySmokeUtc $ExpectedStart
    $observedUtc = ConvertTo-ReadySmokeUtc $ObservedProcess.StartTime
    if ($expectedUtc.Ticks -ne $observedUtc.Ticks) { throw 'PROCESS_TIMESTAMP_MISMATCH' }
}
function ConvertFrom-ReadySmokeDiagnostic([string]$Text, [int]$ExitCode, [int]$ExpectedPID) {
    $pattern = '^PID=(\d+) state=(UNINITIALIZED|REQUESTED|INITIALIZING|READY|FAILED|SHUTDOWN) PIN=([01]) bootstrapTID=(\d+) count=(\d+) callbackCapable=([01]) installed=([01])\r?$'
    $matchesFound = [regex]::Matches($Text, $pattern, [Text.RegularExpressions.RegexOptions]::Multiline)
    if ($matchesFound.Count -ne 1) {
        if ($ExitCode -eq 3 -and $matchesFound.Count -eq 0) { return $null }
        throw 'DIAGNOSTIC_INVALID_OUTPUT'
    }
    $match = $matchesFound[0]
    if ([uint32]$match.Groups[1].Value -ne $ExpectedPID) { throw 'DIAGNOSTIC_PID_MISMATCH' }
    $fields = [pscustomobject]@{PID=$ExpectedPID;State=$match.Groups[2].Value;PIN=[int]$match.Groups[3].Value;
        BootstrapTID=[uint32]$match.Groups[4].Value;Count=[uint64]$match.Groups[5].Value;
        CallbackCapable=[int]$match.Groups[6].Value;Installed=[int]$match.Groups[7].Value}
    if (($ExitCode -eq 0 -and $fields.State -ne 'READY') -or
        ($ExitCode -eq 4 -and $fields.State -eq 'READY') -or $ExitCode -notin @(0,4)) { throw 'DIAGNOSTIC_EXIT_STATE_MISMATCH' }
    return $fields
}
function Invoke-ReadySmokeDiagnostic($Config, $GameProcess) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Config.Diagnostic
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $info.RedirectStandardError = $true
    $info.Arguments = '--pid {0} --expected-asi "{1}"{2}' -f $GameProcess.Id,$Config.SourceAsi,$Config.DiagnosticTail
    $tool = [Diagnostics.Process]::new(); $tool.StartInfo = $info
    try {
        if (!$tool.Start()) { throw 'DIAGNOSTIC_START_FAILED' }
        $stdout = $tool.StandardOutput.ReadToEndAsync(); $stderr = $tool.StandardError.ReadToEndAsync()
        if (!$tool.WaitForExit([int]($Config.DiagnosticTimeoutSeconds * 1000))) {
            # Only the diagnostic child just started, NEVER the GTA process.
            $tool.Kill(); $tool.WaitForExit()
            [IO.File]::AppendAllText((Join-Path $Config.EvidenceDirectory 'diagnostic-timeout.log'),$stdout.Result + $stderr.Result)
            throw 'DIAGNOSTIC_TIMEOUT'
        }
        return [pscustomobject]@{ExitCode=$tool.ExitCode;Text=($stdout.Result + $stderr.Result)}
    } finally { $tool.Dispose() }
}
# An exited entry can still be enumerated while its retained handle exists.
# A PID alone never identifies this session; creation time and path are required.
function Test-ReadySmokeCleanupCandidates($Candidates, $ExpectedPath, $ExpectedPID, $ExpectedStart) {
    $blocked = @()
    foreach ($candidate in @($Candidates)) {
        if ($null -eq $candidate) { continue }
        if ($candidate.Exited) { continue }
        if ($candidate.Error) { $blocked += $candidate; continue }
        if ([string]::Equals([IO.Path]::GetFullPath($candidate.Path),
            [IO.Path]::GetFullPath($ExpectedPath), [StringComparison]::OrdinalIgnoreCase)) {
            $sameSession = $candidate.Id -eq $ExpectedPID -and
                (ConvertTo-ReadySmokeUtc $candidate.StartTime).Ticks -eq (ConvertTo-ReadySmokeUtc $ExpectedStart).Ticks
            $blocked += [pscustomobject]@{Id=$candidate.Id;Path=$candidate.Path;StartTime=$candidate.StartTime;
                Exited=$false;Error=$null;SameSession=$sameSession}
        }
    }
    return $blocked
}
function Get-ReadySmokeCleanupCandidates($ExpectedPath) {
    $name = [IO.Path]::GetFileNameWithoutExtension($ExpectedPath)
    foreach ($candidate in [Diagnostics.Process]::GetProcessesByName($name)) {
        try {
            $candidate.Refresh()
            if ($candidate.HasExited) { continue }
            $snapshot = Get-ReadySmokeProcessSnapshot $candidate
            if ($candidate.WaitForExit(0)) { continue }
            [pscustomobject]@{Id=$snapshot.Id;Path=$snapshot.Path;StartTime=$snapshot.StartTime;Exited=$false;Error=$null}
        } catch {
            # A concurrently exiting entry is harmless only when its retained
            # handle confirms exit. Access denied/unknown identity blocks cleanup.
            $confirmedExit = $false
            try { $confirmedExit = $candidate.WaitForExit(0) } catch { }
            if (!$confirmedExit) {
                [pscustomobject]@{Id=$candidate.Id;Path=$null;StartTime=$null;Exited=$false;Error=$_.Exception.Message}
            }
        } finally { $candidate.Dispose() }
    }
}
function Wait-ReadySmokeCleanupSafe($Game, $Config, $ExpectedStart, $Hooks) {
    $seconds = 120
    if ($Config.PSObject.Properties['ExitTimeoutSeconds']) { $seconds = $Config.ExitTimeoutSeconds }
    $clock = [Diagnostics.Stopwatch]::StartNew()
    do {
        $exited = $null -eq $Game
        if ($Game) { $Game.Refresh(); $exited = $Game.WaitForExit(0) }
        $candidates = if ($Hooks.ContainsKey('CleanupCandidates')) { @(& $Hooks.CleanupCandidates) }
            else { @(Get-ReadySmokeCleanupCandidates $Config.Exe) }
        $blocked = @(Test-ReadySmokeCleanupCandidates $candidates $Config.Exe $(if ($Game) { $Game.Id } else { 0 }) $ExpectedStart)
        $reason = if (!$exited) { 'CLEANUP_EXIT_TIMEOUT' }
            elseif (@($blocked | Where-Object Error).Count) { 'CLEANUP_PROCESS_INACCESSIBLE' }
            elseif ($blocked.Count) { 'CLEANUP_RELEVANT_PROCESS_RUNNING' } else { 'EXIT_AND_INSTALLATION_QUIESCENT' }
        [IO.File]::AppendAllText((Join-Path $Config.EvidenceDirectory 'cleanup-wait.jsonl'),
            (([pscustomobject]@{TimeUtc=[DateTime]::UtcNow.ToString('o');SessionExited=$exited;Reason=$reason;Candidates=$candidates;Blocked=$blocked} |
                ConvertTo-Json -Depth 6 -Compress)) + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
        if ($exited -and !$blocked.Count) { return }
        if ($clock.Elapsed.TotalSeconds -ge $seconds) { throw $reason }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Remove-ReadySmokeExperimental($Target, $ExpectedHash) {
    foreach ($directory in @((Split-Path -Parent $Target),(Split-Path -Parent (Split-Path -Parent $Target)))) {
        if ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'CLEANUP_REPARSE_DIRECTORY' }
    }
    if ((Get-Item -LiteralPath $Target).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'CLEANUP_REPARSE_TARGET' }
    if ((Get-FileHash -LiteralPath $Target -Algorithm SHA256).Hash -ne $ExpectedHash) { throw 'CLEANUP_TARGET_CHANGED' }
    Remove-Item -LiteralPath $Target
    if (Test-Path -LiteralPath $Target) { throw 'CLEANUP_TARGET_REMAINS' }
}

# Domain-specific session function; hooks are used only by offline tests after
# dot-sourcing. The public command has no bypass of its interactive SIM prompt.
function Invoke-ReadySmokeSession($Config, [hashtable]$Hooks = @{}) {
    $resultPath = Join-Path $Config.EvidenceDirectory 'result.json'
    $result = [ordered]@{Status='INCONCLUSIVO';Reason='SESSION_NOT_COMPLETED';PID=$null;StartTimeUtc=$null;
        Ready=$null;StatesObserved=@();TransitionDirectlyObserved=$false;GameExitCode=$null;
        UserObservation=$null;CleanupPending=$false;ExperimentalRemoved=$false;OriginalsVerified=$false;
        Originals=@();Errors=@();ForcedGameTermination=$false;Recovery=$null;CleanupReason=$null;ExitConfirmed=$false}
    $game = $null; $installed = $false; $expectedHash = $null; $startUtc = $null
    try {
        if (@($Config.Originals).Count -ne 13) { throw 'ORIGINAL_MANIFEST_COUNT' }
        foreach ($directory in @($Config.GameRoot,(Split-Path -Parent $Config.TargetAsi))) {
            if ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'INSTALL_REPARSE_DIRECTORY' }
        }
        if ([IO.Path]::GetFullPath($Config.TargetAsi) -ne
            [IO.Path]::GetFullPath((Join-Path $Config.GameRoot 'scripts\GrandMPNativeCounterExperimental.asi'))) { throw 'TARGET_PATH_MISMATCH' }
        if (Test-Path -LiteralPath $Config.TargetAsi) { throw 'TARGET_ALREADY_EXISTS' }
        $expectedHash = (Get-FileHash -LiteralPath $Config.SourceAsi -Algorithm SHA256).Hash
        $data = [IO.File]::ReadAllBytes($Config.SourceAsi)
        $file = [IO.File]::Open($Config.TargetAsi,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
        $installed = $true; $result.CleanupPending = $true
        try { $file.Write($data,0,$data.Length) } finally { $file.Dispose() }
        if ((Get-FileHash -LiteralPath $Config.TargetAsi -Algorithm SHA256).Hash -ne $expectedHash) { throw 'INSTALL_HASH_MISMATCH' }
        Write-ReadySmokeJson $resultPath $result
        $info = [Diagnostics.ProcessStartInfo]::new()
        $info.FileName = $Config.Exe; $info.Arguments = $Config.Arguments; $info.WorkingDirectory = $Config.GameRoot
        $info.UseShellExecute = $false
        $info.EnvironmentVariables['GRANDMP_NATIVE_COUNTER_ASYNC_READY'] = '1'
        foreach ($key in $Config.ExtraEnvironment.Keys) { $info.EnvironmentVariables[$key] = $Config.ExtraEnvironment[$key] }
        if ($Hooks.ContainsKey('Start')) { $game = & $Hooks.Start $info }
        else {
            $game = [Diagnostics.Process]::new(); $game.StartInfo = $info
            try { if (!$game.Start()) { throw 'GAME_START_FAILED' } }
            catch { $game.Dispose(); $game=$null; throw }
        }
        # Retain the process handle for identity/exit; never reacquire by PID for waiting.
        $null = $game.Handle
        $result.PID = $game.Id
        $startUtc = ConvertTo-ReadySmokeUtc $game.StartTime
        $result.StartTimeUtc = $startUtc.ToString('o')
        Write-ReadySmokeJson (Join-Path $Config.EvidenceDirectory 'launch.json') $result
        $clock = [Diagnostics.Stopwatch]::StartNew()
        while (!$game.HasExited) {
            if ($Hooks.ContainsKey('Identity')) { & $Hooks.Identity $game $startUtc }
            else { Assert-ReadySmokeIdentity $game.Id $Config.Exe $startUtc (Get-ReadySmokeProcessSnapshot $game) }
            if ($Hooks.ContainsKey('Diagnose')) { $sample = & $Hooks.Diagnose $game }
            else { $sample = Invoke-ReadySmokeDiagnostic $Config $game }
            $record = [pscustomobject]@{TimeUtc=[DateTime]::UtcNow.ToString('o');ExitCode=$sample.ExitCode;Fields=$null;Output=$sample.Text}
            [IO.File]::AppendAllText((Join-Path $Config.EvidenceDirectory 'diagnostic-raw.jsonl'),
                ($record | ConvertTo-Json -Depth 5 -Compress) + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
            if ($game.HasExited) { break }
            $fields = ConvertFrom-ReadySmokeDiagnostic $sample.Text $sample.ExitCode $game.Id
            $record.Fields = $fields
            [IO.File]::AppendAllText((Join-Path $Config.EvidenceDirectory 'diagnostic.jsonl'),
                ($record | ConvertTo-Json -Depth 5 -Compress) + [Environment]::NewLine,[Text.UTF8Encoding]::new($false))
            if ($fields) {
                $result.StatesObserved += $fields.State
                if ($fields.Count -ne 0 -or $fields.CallbackCapable -ne 0 -or $fields.Installed -ne 0) { $result.Status='FAIL'; $result.Reason='UNSAFE_COUNTER_OR_FLAGS'; break }
                if ($fields.State -in @('FAILED','SHUTDOWN')) { $result.Status='FAIL'; $result.Reason='BOOTSTRAP_' + $fields.State; break }
                if ($result.Ready -and $fields.State -ne 'READY') { $result.Status='FAIL'; $result.Reason='READY_STATE_REGRESSION'; break }
                if ($fields.State -eq 'READY') {
                    if ($fields.PIN -ne 1 -or !$fields.BootstrapTID) { $result.Status='FAIL'; $result.Reason='READY_INVARIANTS_FAILED'; break }
                    if (!$result.Ready) { Write-Host ("READY PIN={0} contador={1} callbackCapable={2} installed={3}. Verifique o jogo e saia pelo menu." -f $fields.PIN,$fields.Count,$fields.CallbackCapable,$fields.Installed) }
                    $result.Ready = $fields; $result.Reason='READY_OBSERVED_AWAITING_USER_EXIT'
                }
            }
            elseif ($result.Ready) { $result.Reason='DIAGNOSTIC_BECAME_INCONCLUSIVE'; break }
            if ($Hooks.ContainsKey('AfterSample')) { & $Hooks.AfterSample $game $record }
            if (!$result.Ready -and $clock.Elapsed.TotalSeconds -ge $Config.ReadyTimeoutSeconds) { $result.Reason='READY_TIMEOUT'; break }
            Write-ReadySmokeJson $resultPath $result
            Start-Sleep -Milliseconds $Config.PollMilliseconds
        }
        if (!$result.Ready -and $game.HasExited -and $result.Reason -eq 'SESSION_NOT_COMPLETED') { $result.Reason='GAME_EXITED_BEFORE_READY' }
    } catch {
        $result.Errors += $_.Exception.ToString(); $result.Reason = $_.Exception.Message
        Write-ReadySmokeJson (Join-Path $Config.EvidenceDirectory 'exception.json') @{Error=$_.Exception.ToString();TimeUtc=[DateTime]::UtcNow.ToString('o')}
    } finally {
        # Preserve a pending-cleanup report BEFORE waiting, including Ctrl+C or
        # console interruption. Never kill GTA or delete a live session's ASI.
        if ($installed) { $result.Recovery = "Feche o GTA normalmente. Só depois remova o experimental verificado em: $($Config.TargetAsi)" }
        Write-ReadySmokeJson $resultPath $result
        $exited = $null -eq $game
        try {
            if ($game -and $Hooks.ContainsKey('BeforeWait')) { & $Hooks.BeforeWait $game }
            Write-Host 'Aguardando saída normal e confirmação da instalação livre. O script não encerrará o GTA.'
            Wait-ReadySmokeCleanupSafe $game $Config $startUtc $Hooks
            $exited = $true; $result.ExitConfirmed = $true
            if ($game) { $result.GameExitCode = $game.ExitCode }
            if ($installed) {
                # Re-enumerate immediately before deletion; an unrelated/new PID
                # in this installation must also prevent removal of its ASI.
                Wait-ReadySmokeCleanupSafe $game $Config $startUtc $Hooks
                if ($Hooks.ContainsKey('Cleanup')) { & $Hooks.Cleanup $Config.TargetAsi $expectedHash }
                else { Remove-ReadySmokeExperimental $Config.TargetAsi $expectedHash }
                $result.ExperimentalRemoved = !(Test-Path -LiteralPath $Config.TargetAsi)
                $result.CleanupPending = !$result.ExperimentalRemoved
                if (!$result.ExperimentalRemoved) { throw 'CLEANUP_TARGET_REMAINS' }
                $result.CleanupReason = 'EXPERIMENTAL_REMOVED_AFTER_CONFIRMED_EXIT'
            }
        } catch {
            $result.Errors += $_.Exception.ToString()
            $result.CleanupReason = $_.Exception.Message
            $result.Reason = 'CLEANUP_FAILED'; $result.CleanupPending = $installed
        }

        # Registrar o encerramento mesmo quando a limpeza falhar.
        # Nunca usar essa observacao para autorizar remocao insegura.
        if ($game -and $null -eq $result.GameExitCode) {
            try {
                if ($game.WaitForExit(0)) {
                    $result.ExitConfirmed = $true
                    $result.GameExitCode = $game.ExitCode
                }
            } catch {
                $result.Errors += (
                    'PROCESS_EXIT_QUERY_FAILED: ' +
                    $_.Exception.Message
                )
            }
        }

        try {
            $result.Originals = @($Config.Originals | ForEach-Object {
                [pscustomobject]@{Path=$_.Path;Expected=$_.SHA256;Actual=(Get-FileHash -LiteralPath $_.Path -Algorithm SHA256).Hash}
            })
            $result.OriginalsVerified = @($result.Originals | Where-Object { $_.Expected -ne $_.Actual }).Count -eq 0
            if (!$result.OriginalsVerified) { $result.Status='FAIL'; $result.Reason='ORIGINAL_HASH_CHANGED' }
        } catch { $result.Errors += $_.Exception.ToString(); $result.Reason='ORIGINAL_VERIFICATION_FAILED' }
        if ($result.Ready -and $exited -and !$result.CleanupPending -and $result.OriginalsVerified -and !$result.Errors.Count -and
            $result.Status -ne 'FAIL' -and $result.Reason -eq 'READY_OBSERVED_AWAITING_USER_EXIT') {
            Write-ReadySmokeJson $resultPath $result
            try {
                if ($Hooks.ContainsKey('Observe')) { $answer = & $Hooks.Observe }
                else { $answer = Read-Host 'GTA abriu/continuou normalmente e você saiu pelo menu? SIM, NAO ou INCONCLUSIVO' }
                $result.UserObservation = $answer
                if ($answer -ceq 'SIM') { $result.Status='PASS'; $result.Reason='READY_VERIFIED_NORMAL_EXIT_AND_CLEANUP' }
                elseif ($answer -ceq 'NAO') { $result.Status='FAIL'; $result.Reason='USER_REPORTED_GAME_FAILURE' }
                else { $result.Reason='USER_OBSERVATION_UNCONFIRMED' }
            } catch { $result.Errors += $_.Exception.ToString(); $result.Reason='USER_OBSERVATION_FAILED' }
        }
        $result.TransitionDirectlyObserved = ($result.StatesObserved -contains 'REQUESTED') -and ($null -ne $result.Ready)
        if (!$result.CleanupPending) { $result.Recovery = $null }
        Write-ReadySmokeJson $resultPath $result
        if ($game) { $game.Dispose() }
    }
    return [pscustomobject]$result
}

function Invoke-PhysicalReadySmokeMain {
    $repoRoot = Split-Path -Parent $PSScriptRoot
    $gameRoot = 'D:\Jogos\GrandMP-Smoke'
    $buildRoot = Join-Path $repoRoot 'build\alpha036-win32\client\windows\Debug'
    $evidence = Join-Path $repoRoot ('build\physical-ready-smoke\' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $evidence | Out-Null
    $resultPath = Join-Path $evidence 'result.json'
    Write-ReadySmokeJson (Join-Path $evidence 'preflight.json') @{Status='IN_PROGRESS';GameRoot=$gameRoot}
    try {
        foreach ($directory in @($gameRoot,(Join-Path $gameRoot 'scripts'))) {
            if ((Get-Item -LiteralPath $directory).Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'PREFLIGHT_REPARSE_DIRECTORY' }
        }
        if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -eq 'gta_sa').Count) { throw 'PREFLIGHT_GTA_ALREADY_RUNNING' }
        $source = Join-Path $buildRoot 'GrandMPNativeCounterExperimental.asi'
        $diagnostic = Join-Path $buildRoot 'grandmp_native_counter_diagnostic.exe'
        $hashes = @{
            (Join-Path $gameRoot 'gta_sa.exe')='A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'
            (Join-Path $gameRoot 'dinput8.dll')='29FEE0AA46414864AD1F65B2689B470BFB2CF2F09A94E2AFD271E68F2E16743B'
            (Join-Path $gameRoot 'scripts\GrandMP.asi')='1EB0E9E22591402FA6002363DD0FC22B873B27F8224EA11C1FA3B356EEC7D2FA'
            $source='8DE9EDAC29035374AB37DC20312D5817E84335C10AFFC6E0B0984209EE38AF71'
            $diagnostic='833DD0D195CB3C8953DC81552F3313860561342D9B554238E257E2F9D7DDF8FD'
        }
        $peImages = @()
        foreach ($item in $hashes.GetEnumerator()) {
            if ((Get-FileHash -LiteralPath $item.Key -Algorithm SHA256).Hash -ne $item.Value) { throw "PREFLIGHT_HASH_MISMATCH: $($item.Key)" }
            # Same PE32/i386 check used by the existing runtime launcher.
            $bytes = [IO.File]::ReadAllBytes($item.Key); $pe = [BitConverter]::ToInt32($bytes,0x3C)
            if ($pe -lt 0 -or $pe -gt $bytes.Length-56 -or [BitConverter]::ToUInt16($bytes,0) -ne 0x5A4D -or
                [BitConverter]::ToUInt32($bytes,$pe) -ne 0x4550 -or [BitConverter]::ToUInt16($bytes,$pe+4) -ne 0x14C -or
                [BitConverter]::ToUInt16($bytes,$pe+24) -ne 0x10B) { throw 'PREFLIGHT_NOT_PE32_I386' }
            $imageBase = [BitConverter]::ToUInt32($bytes,$pe+52)
            if ($item.Key -eq (Join-Path $gameRoot 'gta_sa.exe') -and $imageBase -ne 0x400000) { throw 'PREFLIGHT_GAME_IMAGE_BASE' }
            $peImages += [pscustomobject]@{Path=$item.Key;Machine='014C';Magic='010B';ImageBase=('{0:X8}' -f $imageBase);Bytes=$bytes.Length}
        }
        if ([Diagnostics.FileVersionInfo]::GetVersionInfo((Join-Path $gameRoot 'dinput8.dll')).FileVersion -ne '7.7.0') { throw 'PREFLIGHT_LOADER_VERSION' }
        $active = @(Get-ChildItem -LiteralPath $gameRoot -Recurse -File -Filter '*.asi')
        if ($active.Count -ne 1 -or $active[0].FullName -ne (Join-Path $gameRoot 'scripts\GrandMP.asi')) { throw 'PREFLIGHT_ASI_INVENTORY' }
        if (!(Test-Path -LiteralPath (Join-Path $gameRoot 'scripts\III.VC.SA.WindowedMode.asi.disabled'))) { throw 'PREFLIGHT_WINDOWED_MODE' }
        $relative = @('By Thirore.txt','dinput8.dll','eax.dll','global.ini','gta_sa.exe','ogg.dll','stream.ini','vorbis.dll','vorbisFile.dll',
            'scripts\global.ini','scripts\GrandMP.asi','scripts\III.VC.SA.WindowedMode.asi.disabled','scripts\III.VC.SA.WindowedMode.ini')
        $originals = @($relative | ForEach-Object { $path = Join-Path $gameRoot $_; [pscustomobject]@{Path=$path;SHA256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash} })
        Write-ReadySmokeJson (Join-Path $evidence 'preflight.json') @{Status='PASS';Originals=$originals;Hashes=$hashes;
            SourceAsi=$source;GameRoot=$gameRoot;ActiveAsis=@($active.FullName);WindowedMode='disabled';OptInScope='child process only';
            PeImages=$peImages;LoaderVersion='7.7.0';DiagnosticInterface='--pid decimal --expected-asi file.asi'}
        Write-Host "Relatórios: $evidence"
        if ($CheckOnly) { Write-ReadySmokeJson $resultPath @{Status='INCONCLUSIVO';Reason='CHECK_ONLY_NO_GAME_STARTED'}; return }
        if ((Read-Host 'Digite SIM para instalar temporariamente e iniciar UMA sessão somente READY/PIN') -cne 'SIM') {
            Write-ReadySmokeJson $resultPath @{Status='INCONCLUSIVO';Reason='USER_CANCELLED_BEFORE_INSTALL'}; return
        }
        if (@(Get-Process -ErrorAction SilentlyContinue | Where-Object ProcessName -eq 'gta_sa').Count) { throw 'PREFLIGHT_GTA_STARTED_DURING_CONFIRMATION' }
        foreach ($original in $originals) { if ((Get-FileHash -LiteralPath $original.Path -Algorithm SHA256).Hash -ne $original.SHA256) { throw 'PREFLIGHT_ORIGINAL_CHANGED' } }
        foreach ($item in $hashes.GetEnumerator()) { if ((Get-FileHash -LiteralPath $item.Key -Algorithm SHA256).Hash -ne $item.Value) { throw 'PREFLIGHT_ARTIFACT_CHANGED' } }
        $active = @(Get-ChildItem -LiteralPath $gameRoot -Recurse -File -Filter '*.asi')
        if ($active.Count -ne 1 -or $active[0].FullName -ne (Join-Path $gameRoot 'scripts\GrandMP.asi')) { throw 'PREFLIGHT_ASI_CHANGED_DURING_CONFIRMATION' }
        $config = [pscustomobject]@{EvidenceDirectory=$evidence;Exe=(Join-Path $gameRoot 'gta_sa.exe');Arguments='';GameRoot=$gameRoot;
            SourceAsi=$source;TargetAsi=(Join-Path $gameRoot 'scripts\GrandMPNativeCounterExperimental.asi');Diagnostic=$diagnostic;
            Originals=$originals;ReadyTimeoutSeconds=$ReadyTimeoutSeconds;ExitTimeoutSeconds=$ExitTimeoutSeconds;DiagnosticTimeoutSeconds=5;PollMilliseconds=500;
            ExtraEnvironment=@{};DiagnosticTail=''}
        $result = Invoke-ReadySmokeSession $config
        Write-Host ("{0}: {1}" -f $result.Status,$result.Reason)
        if ($result.CleanupPending) { Write-Warning $result.Recovery }
    } catch {
        if (!(Test-Path -LiteralPath (Join-Path $evidence 'launch.json'))) {
            Write-ReadySmokeJson (Join-Path $evidence 'preflight.json') @{Status='FAIL';Reason=$_.Exception.Message;Error=$_.Exception.ToString();GameRoot=$gameRoot}
        }
        if (Test-Path -LiteralPath $resultPath) {
            Write-ReadySmokeJson (Join-Path $evidence 'controller-error.json') @{Error=$_.Exception.ToString()}
        } else {
            Write-ReadySmokeJson $resultPath @{Status='INCONCLUSIVO';Reason=$_.Exception.Message;Error=$_.Exception.ToString();Phase='PREFLIGHT'}
        }
        Write-Warning "Interrompido. Consulte $resultPath"
    }
}
if ($MyInvocation.InvocationName -ne '.') { Invoke-PhysicalReadySmokeMain }
