param([string]$BinaryDirectory)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'scripts\Invoke-PhysicalReadySmoke.ps1')
if (-not $BinaryDirectory) { $BinaryDirectory = Join-Path $repoRoot 'build\alpha036-win32\client\windows\Debug' }
$hostExe = Join-Path $BinaryDirectory 'grandmp_native_counter_host.exe'
$sourceAsi = Join-Path $BinaryDirectory 'GrandMPNativeCounterExperimental.asi'
$diagnostic = Join-Path $BinaryDirectory 'grandmp_native_counter_diagnostic.exe'
foreach ($path in @($hostExe, $sourceAsi, $diagnostic)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Build Win32 existente requerida: $path" }
}
$testRoot = Join-Path $repoRoot ('build\physical-ready-smoke-tests\' + [guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($testRoot) | Out-Null
$script:passed = 0
function Assert-Case([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAIL: $Name" }
    $script:passed++
    Write-Host "PASS: $Name"
}
function Assert-Rejected([scriptblock]$Operation, [string]$Name) {
    $rejected = $false
    try { & $Operation | Out-Null } catch { $rejected = $true }
    Assert-Case $rejected $Name
}
$utc = [datetime]::UtcNow
$observed = [pscustomobject]@{ Id = 1234; StartTime = $utc.ToLocalTime(); Path = $hostExe }
Assert-ReadySmokeIdentity -ExpectedPID 1234 -ExpectedPath $hostExe -ExpectedStart $utc -ObservedProcess $observed
Assert-Case $true 'UTC/local DateTime identity accepted'
Assert-Case ((ConvertTo-ReadySmokeUtc $utc).Ticks -eq $utc.Ticks) 'UTC DateTime remains UTC'
Assert-Rejected { Assert-ReadySmokeIdentity -ExpectedPID 5678 -ExpectedPath $hostExe -ExpectedStart $utc -ObservedProcess $observed } 'incorrect PID rejected'
Assert-Rejected { Assert-ReadySmokeIdentity -ExpectedPID 1234 -ExpectedPath $hostExe -ExpectedStart $utc.AddSeconds(-1) -ObservedProcess $observed } 'incorrect creation timestamp rejected'
Assert-Rejected { Assert-ReadySmokeIdentity -ExpectedPID 1234 -ExpectedPath ($hostExe + '.wrong') -ExpectedStart $utc -ObservedProcess $observed } 'incorrect image path rejected'
$roundTrip = ($utc.ToString('o') | ConvertTo-Json | ConvertFrom-Json)
Assert-ReadySmokeIdentity -ExpectedPID 1234 -ExpectedPath $hostExe -ExpectedStart $roundTrip -ObservedProcess $observed
Assert-Case $true 'serialized timestamp explicitly normalized without string/DateTime comparison'
Assert-Rejected { ConvertTo-ReadySmokeUtc '2026-10-14T23:41:08' } 'timestamp string without timezone rejected'
$unspecified = [datetime]::SpecifyKind($utc, [DateTimeKind]::Unspecified)
Assert-Rejected { ConvertTo-ReadySmokeUtc $unspecified } 'unspecified timestamp timezone rejected'
$validText = 'PID=1234 state=READY PIN=1 bootstrapTID=4321 count=0 callbackCapable=0 installed=0'
$parsed = ConvertFrom-ReadySmokeDiagnostic -Text $validText -ExitCode 0 -ExpectedPID 1234
Assert-Case ($null -ne $parsed) 'complete READY diagnostic accepted'
foreach ($bad in @($validText.Replace('PID=1234','PID=9999'), $validText.Replace(' PIN=1',''), ($validText + "`n" + $validText.Replace('PIN=1','PIN=0')))) {
    Assert-Rejected { ConvertFrom-ReadySmokeDiagnostic -Text $bad -ExitCode 0 -ExpectedPID 1234 } 'invalid/ambiguous diagnostic rejected'
}
# Regression: stale enumeration and PID reuse do not identify a live session.
$gone = [pscustomobject]@{Id=1234;Path=$hostExe;StartTime=$utc;Exited=$true;Error=$null}
Assert-Case (@(Test-ReadySmokeCleanupCandidates @($gone) $hostExe 1234 $utc).Count -eq 0) 'exited enumerated process does not block cleanup'
$reused = [pscustomobject]@{Id=1234;Path=$hostExe;StartTime=$utc.AddSeconds(5);Exited=$false;Error=$null}
$reusedResult = @(Test-ReadySmokeCleanupCandidates @($reused) $hostExe 1234 $utc)
Assert-Case ($reusedResult.Count -eq 1 -and !$reusedResult[0].SameSession) 'PID reused in same installation blocks cleanup as a different session'
$elsewhere = [pscustomobject]@{Id=1234;Path=($hostExe + '.elsewhere');StartTime=$utc.AddSeconds(5);Exited=$false;Error=$null}
Assert-Case (@(Test-ReadySmokeCleanupCandidates @($elsewhere) $hostExe 1234 $utc).Count -eq 0) 'PID reused elsewhere is not the monitored session or installation'
$inaccessible = [pscustomobject]@{Id=1234;Path=$null;StartTime=$null;Exited=$false;Error='access denied'}
Assert-Case (@(Test-ReadySmokeCleanupCandidates @($inaccessible) $hostExe 1234 $utc).Count -eq 1) 'inaccessible candidate blocks cleanup conservatively'
function Invoke-LabCase([string]$Name) {
    $run = Join-Path $testRoot $Name
    [IO.Directory]::CreateDirectory($run) | Out-Null
    $originals = @()
    for ($i = 0; $i -lt 13; $i++) {
        $path = Join-Path $run ("original-$i.txt")
        [IO.File]::WriteAllText($path, "disposable original $i")
        $originals += @{ Path = $path; SHA256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash }
    }
    [IO.Directory]::CreateDirectory((Join-Path $run 'scripts')) | Out-Null
    $target = Join-Path $run 'scripts\GrandMPNativeCounterExperimental.asi'
    $eventName = 'Local\GrandMPReadySmokeTest-' + [guid]::NewGuid().ToString('N')
    $event = New-Object Threading.EventWaitHandle($false, [Threading.EventResetMode]::ManualReset, $eventName)
    $config = [pscustomobject]@{
        EvidenceDirectory = $run; Exe = $hostExe; Arguments = ('"' + $target + '" async_observe')
        GameRoot = $run; SourceAsi = $sourceAsi; TargetAsi = $target; Diagnostic = $diagnostic
        Originals = $originals; ReadyTimeoutSeconds = 1; ExitTimeoutSeconds = 1; DiagnosticTimeoutSeconds = 3; PollMilliseconds = 50
        ExtraEnvironment = @{ GRANDMP_COUNTER_OBSERVER_EXIT = $eventName }; DiagnosticTail = ' --lab-profile'
    }
    $hooks = @{
        BeforeWait = { param($process) $event.Set() | Out-Null }.GetNewClosure()
        Observe = { 'SIM' }
        AfterSample = {
            param($process, $record)
            if ($record.Fields -and $record.Fields.State -eq 'READY') {
                if ($process.HasExited -or !(Test-Path -LiteralPath $target)) { throw 'experimental missing during live callback' }
                $event.Set() | Out-Null
            }
        }.GetNewClosure()
    }
    switch ($Name) {
        'wrong_pid' { $hooks.Identity = { param($process, $start) if ($process.Id -ne ($process.Id + 1)) { throw 'PID_MISMATCH' } } }
        'timeout' {
            $hooks.Diagnose = { param($process) @{ ExitCode = 4; Text = "PID=$($process.Id) state=REQUESTED PIN=0 bootstrapTID=0 count=0 callbackCapable=0 installed=0" } }
            $hooks.Remove('AfterSample')
        }
        'early_exit' { $config.Exe = $env:ComSpec; $config.Arguments = '/d /c exit 0'; $hooks.CleanupCandidates = { @() } }
        'diagnostic_exception' { $hooks.Diagnose = { param($process) throw 'injected diagnostic failure' } }
        'ready_regression' {
            $samples = [pscustomobject]@{ Count = 0 }
            $hooks.Diagnose = {
                param($process)
                $samples.Count++
                if ($samples.Count -eq 1) {
                    return @{ ExitCode = 0; Text = "PID=$($process.Id) state=READY PIN=1 bootstrapTID=1234 count=0 callbackCapable=0 installed=0" }
                }
                return @{ ExitCode = 4; Text = "PID=$($process.Id) state=REQUESTED PIN=0 bootstrapTID=0 count=0 callbackCapable=0 installed=0" }
            }.GetNewClosure()
            $hooks.Remove('AfterSample')
        }
        'diagnostic_lost' {
            $samples = [pscustomobject]@{ Count = 0 }
            $hooks.Diagnose = {
                param($process)
                $samples.Count++
                if ($samples.Count -eq 1) { return @{ ExitCode = 0; Text = "PID=$($process.Id) state=READY PIN=1 bootstrapTID=1234 count=0 callbackCapable=0 installed=0" } }
                return @{ ExitCode = 3; Text = 'INCONCLUSIVO: injected diagnostic evidence loss' }
            }.GetNewClosure()
            $hooks.Remove('AfterSample')
        }
        'unsafe_flags' { $hooks.Diagnose = { param($process) @{ ExitCode = 0; Text = "PID=$($process.Id) state=READY PIN=1 bootstrapTID=1234 count=1 callbackCapable=0 installed=0" } } }
        'cleanup_failure' { $hooks.Cleanup = { param($target, $hash) throw 'injected cleanup failure' } }
        'user_uncertain' { $hooks.Observe = { '' } }
        'late_exit' {
            $state = [pscustomobject]@{Calls=0}
            $hooks.CleanupCandidates = {
                $state.Calls++
                if ($state.Calls -le 3) { return [pscustomobject]@{Id=9999;Path=$hostExe;StartTime=[DateTime]::UtcNow;Exited=$false;Error=$null} }
            }.GetNewClosure()
        }
        'cleanup_timeout' {
            $hooks.CleanupCandidates = { [pscustomobject]@{Id=9999;Path=$hostExe;StartTime=[DateTime]::UtcNow;Exited=$false;Error=$null} }.GetNewClosure()
        }
        'cleanup_inaccessible' {
            $hooks.CleanupCandidates = { [pscustomobject]@{Id=9999;Path=$null;StartTime=$null;Exited=$false;Error='access denied'} }
        }
    }
    try { $result = Invoke-ReadySmokeSession -Config $config -Hooks $hooks }
    finally { $event.Set() | Out-Null; $event.Dispose() }
    Assert-Case (Test-Path -LiteralPath (Join-Path $run 'result.json')) "$Name result preserved"
    if ($Name -in @('success','late_exit')) { Assert-Case ($result.Status -eq 'PASS') 'real read-only diagnostic and normal child exit PASS' }
    else { Assert-Case ($result.Status -ne 'PASS') "$Name cannot report PASS" }
    $expectedReason = @{
        success='READY_VERIFIED_NORMAL_EXIT_AND_CLEANUP'; late_exit='READY_VERIFIED_NORMAL_EXIT_AND_CLEANUP'; cleanup_timeout='CLEANUP_FAILED'; cleanup_inaccessible='CLEANUP_FAILED'; wrong_pid='PID_MISMATCH'; timeout='READY_TIMEOUT'
        early_exit='GAME_EXITED_BEFORE_READY'; diagnostic_exception='injected diagnostic failure'
        cleanup_failure='CLEANUP_FAILED'; user_uncertain='USER_OBSERVATION_UNCONFIRMED'; unsafe_flags='UNSAFE_COUNTER_OR_FLAGS'
        diagnostic_lost='DIAGNOSTIC_BECAME_INCONCLUSIVE'; ready_regression='READY_STATE_REGRESSION'
    }
    Assert-Case ($result.Reason -eq $expectedReason[$Name]) "$Name specific outcome reason"
    if ($Name -eq 'unsafe_flags') { Assert-Case ($result.Status -eq 'FAIL') 'nonzero counter explicitly FAIL' }
    if ($Name -eq 'ready_regression') { Assert-Case ($result.Status -eq 'FAIL') 'READY regression explicitly FAIL' }
    Assert-Case ($result.GameExitCode -eq 0) "$Name disposable process completed normally"
    Assert-Case ($result.CleanupPending -eq ($Name -in @('cleanup_failure','cleanup_timeout','cleanup_inaccessible'))) "$Name cleanup pending matches actual outcome"
    if ($Name -in @('cleanup_failure','cleanup_timeout','cleanup_inaccessible')) {
        Assert-Case (Test-Path -LiteralPath $target) 'cleanup failure preserves experimental and records pending recovery'
        Assert-Case ([bool]$result.Recovery) 'pending cleanup contains manual recovery instruction'
        if ($Name -eq 'cleanup_timeout') { Assert-Case ($result.CleanupReason -eq 'CLEANUP_RELEVANT_PROCESS_RUNNING') 'live installation timeout has specific evidence' }
        if ($Name -eq 'cleanup_inaccessible') { Assert-Case ($result.CleanupReason -eq 'CLEANUP_PROCESS_INACCESSIBLE') 'inaccessible process remains inconclusive'
        }
        # Manual recovery simulation, after the disposable session has exited.
        Remove-ReadySmokeExperimental $target (Get-FileHash -LiteralPath $sourceAsi -Algorithm SHA256).Hash
        Assert-Case (!(Test-Path -LiteralPath $target)) 'manual hash-verified recovery removes only experimental'
        $saved = Get-Content -LiteralPath (Join-Path $run 'result.json') -Raw | ConvertFrom-Json
        Assert-Case ($saved.Status -eq 'INCONCLUSIVO' -and $saved.CleanupPending) 'manual recovery never rewrites original inconclusive report'
    }
    else { Assert-Case (-not (Test-Path -LiteralPath $target)) "$Name cleanup occurs only after real child exit" }
    foreach ($item in $originals) { if ((Get-FileHash -LiteralPath $item.Path).Hash -ne $item.SHA256) { throw "Original modified: $($item.Path)" } }
    Assert-Case $true "$Name all 13 disposable originals preserved"
}
foreach ($name in @('success','wrong_pid','timeout','early_exit','diagnostic_exception','cleanup_failure','user_uncertain','unsafe_flags','diagnostic_lost','ready_regression','late_exit','cleanup_timeout','cleanup_inaccessible')) { Invoke-LabCase $name }
Write-Host "Physical READY offline: $script:passed assertions PASS; evidence=$testRoot"
