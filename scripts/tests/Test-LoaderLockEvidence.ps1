$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'scripts\LoaderLockEvidence.ps1')
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path $cdb) -or -not (Test-Path $vsDevCmd)) { throw 'Este teste real requer CDB x86 e MSVC x86.' }

if (-not ('GrandMpLoaderLockProbe.NativeExit' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace GrandMpLoaderLockProbe {
    public static class NativeExit {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeProcess(IntPtr handle, out uint code);
        public static uint Read(IntPtr handle) {
            uint code;
            if (!GetExitCodeProcess(handle, out code))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            return code;
        }
    }
}
'@
}

$testRoot = Join-Path $repoRoot ('build\runtime-evidence-loaderlock-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'cdb-detach\ProbeTarget.cpp'),(Join-Path $PSScriptRoot 'cdb-detach\ProbeDetach.cpp'),(Join-Path $PSScriptRoot 'cdb-detach\LoaderLockModule.cpp') -Destination $testRoot
$compile = '"' + $vsDevCmd + '" -no_logo -arch=x86 -host_arch=x64 && cd /d "' + $testRoot + '" && cl.exe /nologo /EHsc /MD /Od /Zi /Fe:ProbeTarget.exe ProbeTarget.cpp /link /DEBUG:FULL && cl.exe /nologo /EHsc /MD /Od /Zi /LD /Fe:ProbeModule.dll LoaderLockModule.cpp /link /DEBUG:FULL && cl.exe /nologo /EHsc /MD /LD /Fe:ProbeDetach.dll ProbeDetach.cpp'
& $env:ComSpec /d /s /c $compile
if ($LASTEXITCODE -ne 0) { throw 'Compilação do fixture Win32 falhou.' }

function Invoke-LockCase {
    param([string]$Name, [string]$Expected, [switch]$Held, [switch]$MissingType, [switch]$CorruptExpression, [switch]$CorruptGuard, [switch]$MissingEntry)
    $run = Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path $run | Out-Null
    Copy-Item (Join-Path $testRoot 'ProbeTarget.exe'),(Join-Path $testRoot 'ProbeTarget.pdb'),(Join-Path $testRoot 'ProbeModule.dll'),(Join-Path $testRoot 'ProbeModule.pdb'),(Join-Path $testRoot 'ProbeDetach.dll') -Destination $run
    $entryModule = if ($Held) { 'ProbeModule' } else { 'ProbeTarget' }
    $entryImage = if ($Held) { 'ProbeModule.dll' } else { 'ProbeTarget.exe' }
    $entryName = if ($Held) { 'LockedEntry' } elseif ($MissingEntry) { 'DeliberatelyMissingEntry' } else { 'ProbeFunction' }
    Write-LoaderLockDebuggerScripts -RunDirectory $run -PdbDirectory $run -EntryModule $entryModule -EntryImage $entryImage -EntryName $entryName -LoadModule 'ProbeModule'
    $files = @(Get-ChildItem $run -Filter '*.wds')
    if ($files.Count -ne 4) { throw 'Precisam existir somente os quatro scripts da captura LoaderLock.' }
    foreach ($file in $files) {
        $text = [IO.File]::ReadAllText($file.FullName)
        if ($text -match '(?im)^\s*(?:bp|bu|bm|t|p|qd|\.call|eb|ed|ew)\b' -or $text -match '\$\$><|53[Ee]981|53[Bb][Ee][Ee]0') {
            throw "Operação fora do escopo em $($file.Name)."
        }
        if ($text -match '(?m)^\.sym(?:fix|path\+).*;') { throw 'Comandos ou separadores indevidos no caminho gerado.' }
        if ($text -match '(?m)^\.if.*\{\s*$') { throw 'Bloco .if multilinha não permitido.' }
    }
    $capturePath = Join-Path $run 'capture-loader-lock.wds'
    $capture = [IO.File]::ReadAllText($capturePath)
    if ($MissingType) { $capture = $capture.Replace('ntdll!_PEB', 'ntdll!_DeliberatelyMissingPEB') }
    if ($CorruptExpression) { $capture = $capture.Replace('?? ((ntdll!_PEB*)@$peb)->LoaderLock', '?? (') }
    [IO.File]::WriteAllText($capturePath,$capture,[Text.Encoding]::ASCII)
    if ($CorruptGuard) {
        $modulePath = Join-Path $run 'capture-grandmp-load.wds'
        $module = [IO.File]::ReadAllText($modulePath)
        $module = $module.Replace('.if ((@$t8 != 0) & (@$t8 == @$t9))', '.if ((@$t8 != 0)')
        [IO.File]::WriteAllText($modulePath, $module, [Text.Encoding]::ASCII)
    }

    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $cdb
    $start.Arguments = '-cf "' + (Join-Path $run 'boot-initialize.wds') + '" "' + (Join-Path $run 'ProbeTarget.exe') + '"'
    $start.WorkingDirectory = $run
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $debugger = [Diagnostics.Process]::Start($start)
    $stdout = $debugger.StandardOutput.ReadToEndAsync()
    $stderr = $debugger.StandardError.ReadToEndAsync()
    try {
        if (-not $debugger.WaitForExit(45000)) { throw "Sessão CDB inesperadamente parada: $run" }
        if ($debugger.ExitCode -ne 0) {
            [IO.File]::WriteAllText((Join-Path $run 'transcript.txt'), ($stdout.Result + "`n" + $stderr.Result))
            throw "CDB falhou com exit $($debugger.ExitCode): $run"
        }
        $deadline = (Get-Date).AddSeconds(5)
        while (-not (Test-Path (Join-Path $run 'ready.txt')) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 20 }
        if (-not (Test-Path (Join-Path $run 'ready.txt'))) { throw "Alvo não continuou após detach: $run" }
        $targetPid = [int](Get-Content (Join-Path $run 'ready.txt') -Raw)
        $target = Get-Process -Id $targetPid -ErrorAction SilentlyContinue
        if (-not $target) { throw "Alvo não permaneceu vivo após detach: $run" }
        try {
            $targetHandle = $target.Handle
            if (-not $target.WaitForExit(5000)) { throw "Alvo não encerrou normalmente: $run" }
            if ([GrandMpLoaderLockProbe.NativeExit]::Read($targetHandle) -ne 0) { throw "Alvo terminou com código não zero: $run" }
        }
        finally { $target.Dispose() }
        $transcript = $stdout.Result + "`n" + $stderr.Result
        [IO.File]::WriteAllText((Join-Path $run 'transcript.txt'), $transcript)
        if ($transcript -match '(?i)implicit wait|Command file execution failed|Win32 error 0n123' -or
            (-not ($CorruptExpression -or $CorruptGuard) -and $transcript -match '(?i)Syntax error')) {
            throw "Erro inesperado de interpretação do CDB: $run"
        }
        if (($CorruptExpression -or $CorruptGuard) -and $transcript -notmatch '(?i)Syntax error') { throw 'O erro sintático deliberado não foi observado.' }
        if (-not (Test-Path (Join-Path $run 'normal-return.txt')) -or
            (Get-Content (Join-Path $run 'normal-return.txt') -Raw) -ne '0' -or
            (Get-Content (Join-Path $run 'after-detach.txt') -Raw) -ne 'detached' -or
            -not (Test-Path (Join-Path $run 'after-detach-function.txt')) -or
            (Test-Path (Join-Path $run 'unhandled-exception.txt'))) { throw "Alvo não sobreviveu/completou após detach: $run" }
        $log = [IO.File]::ReadAllText((Join-Path $run '01-loader-lock.txt'))
        $result = Get-LoaderLockEvidenceResult -LogText $log
        $result | ConvertTo-Json | Set-Content (Join-Path $run 'result.json') -Encoding UTF8
        if ($result.Status -ne $Expected) { throw "Esperado $Expected, recebido $($result.Status): $($result.Reason). $run" }
        if ($Held -and -not $result.OwnerIsCurrentThread) { throw "Lock adquirido em DllMain não corresponde à thread da captura: $run" }
        if (($MissingEntry -or $CorruptGuard) -and $log -match '(?m)^LL_CAPTURE_BEGIN\r?$') { throw 'Entrada/guard inválido conseguiu disparar captura.' }
        if ($CorruptGuard -and $log -notmatch '(?m)^LL_INCONCLUSIVE_CLEANUP\r?$') { throw 'Guard inválido não tomou o cleanup padrão.' }
        Write-Host "PASS $Name : $($result.Status), alvo vivo após detach e saída normal."
        return $log
    }
    finally {
        if (-not $debugger.HasExited) { $debugger.Kill(); $debugger.WaitForExit() }
        $debugger.Dispose()
        $pidPath = Join-Path $run 'ready.txt'
        if (Test-Path $pidPath) {
            $orphan = Get-Process -Id ([int](Get-Content $pidPath -Raw)) -ErrorAction SilentlyContinue
            if ($orphan) { $orphan.Kill(); $orphan.WaitForExit(); $orphan.Dispose() }
        }
    }
}

for ($i=1; $i -le 3; $i++) { $freeLog = Invoke-LockCase -Name "free-$i" -Expected 'OBSERVADO_LIVRE' }
$null = Invoke-LockCase -Name 'held-in-dllmain' -Held -Expected 'OBSERVADO_OCUPADO'
$null = Invoke-LockCase -Name 'missing-type' -MissingType -Expected 'INCONCLUSIVO'
$null = Invoke-LockCase -Name 'syntax-error' -CorruptExpression -Expected 'INCONCLUSIVO'
$null = Invoke-LockCase -Name 'invalid-guard' -CorruptGuard -Expected 'INCONCLUSIVO'
$null = Invoke-LockCase -Name 'missing-entry' -MissingEntry -Expected 'INCONCLUSIVO'

# Fail-closed interpretation: contradict actual typed evidence, never upgrade it.
$mutations = @(
    @{ Name='void-type'; Text=$freeLog.Replace('_RTL_CRITICAL_SECTION','Void') },
    @{ Name='missing-pdb'; Text=$freeLog.Replace('Symbol Type: PDB','Symbol Type: EXPORT') },
    @{ Name='missing-detach'; Text=([regex]::Replace($freeLog,'(?m)^Detached\r?$','')) },
    @{ Name='inconsistent-recursion'; Text=([regex]::Replace($freeLog,'RecursionCount\s*:\s*0n0','RecursionCount : 0n1')) },
    @{ Name='invalid-read'; Text=$freeLog.Replace('LL_OBJECT_END',"Memory access error`r`nLL_OBJECT_END") },
    @{ Name='implicit-wait'; Text=($freeLog + "`r`nCommand file caused an implicit wait`r`n") }
)
foreach ($mutation in $mutations) {
    if ((Get-LoaderLockEvidenceResult -LogText $mutation.Text).Status -ne 'INCONCLUSIVO') { throw "Interpretação fail-open: $($mutation.Name)" }
    Write-Output "PASS parser $($mutation.Name) : INCONCLUSIVO."
}
Write-Output "8 cenários CDB Win32 e 6 rejeições do parser aprovados. Logs: $testRoot"
