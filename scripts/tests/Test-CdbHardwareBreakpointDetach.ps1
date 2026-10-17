$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $cdb -PathType Leaf)) {
    Write-Output 'CDB hardware breakpoint/detach probe: SKIP (CDB x86 não instalado).'
    exit 0
}
if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf) -and -not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    Write-Output 'CDB hardware breakpoint/detach probe: SKIP (MSVC não instalado).'
    exit 0
}

if (-not ('GrandMpDetachProbe.NativeProcess' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace GrandMpDetachProbe {
    public static class NativeProcess {
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);
        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        public static uint GetExitCode(IntPtr process) {
            uint exitCode;
            if (!GetExitCodeProcess(process, out exitCode)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            return exitCode;
        }

        public static void Close(IntPtr handle) {
            if (handle != IntPtr.Zero && !CloseHandle(handle)) {
                throw new Win32Exception(Marshal.GetLastWin32Error());
            }
        }
    }
}
'@
}

$testRoot = Join-Path $repoRoot ('build\runtime-evidence-detach-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
try {
    Copy-Item (Join-Path $PSScriptRoot 'cdb-detach\ProbeTarget.cpp'),(Join-Path $PSScriptRoot 'cdb-detach\ProbeDetach.cpp'),(Join-Path $PSScriptRoot 'cdb-detach\ProbeModule.cpp') -Destination $testRoot
    if (Test-Path -LiteralPath $vsDevCmd) {
        $compile = '"' + $vsDevCmd + '" -no_logo -arch=x86 -host_arch=x64 && cd /d "' + $testRoot + '" && cl.exe /nologo /EHsc /MD /Od /Zi /Fe:ProbeTarget.exe ProbeTarget.cpp /link /DEBUG:FULL && cl.exe /nologo /EHsc /MD /LD /Fe:ProbeDetach.dll ProbeDetach.cpp && cl.exe /nologo /EHsc /MD /LD /Fe:ProbeModule.dll ProbeModule.cpp'
        & $env:ComSpec /d /s /c $compile
        if ($LASTEXITCODE -ne 0) { throw "Compilação do fixture Win32 falhou: exit $LASTEXITCODE." }
    } else {
        throw 'cl.exe está no PATH, mas VsDevCmd.bat não está disponível para configurar o linker x86.'
    }

    function Invoke-Probe {
        param(
            [Parameter(Mandatory)][string]$Name,
            [ValidateSet('legacy-qd','handled-detach','cancel','validation-failure')][string]$Mode
        )

        $run = Join-Path $testRoot $Name
        New-Item -ItemType Directory -Path $run | Out-Null
        Copy-Item (Join-Path $testRoot 'ProbeTarget.exe'),(Join-Path $testRoot 'ProbeTarget.pdb'),(Join-Path $testRoot 'ProbeDetach.dll'),(Join-Path $testRoot 'ProbeModule.dll') -Destination $run
        if ($Mode -eq 'cancel') { [IO.File]::WriteAllText((Join-Path $run 'cancel.flag'),'cancel') }
        if ($Mode -eq 'validation-failure') { [IO.File]::WriteAllText((Join-Path $run 'validation-failed.flag'),'fail') }

        $log = Join-Path $run 'cdb.log'
        $boot = Join-Path $run 'boot.wds'
        $capture = Join-Path $run 'capture.wds'
        $commands = [System.Collections.Generic.List[string]]::new()
        $commands.Add('.logopen ' + $log)
        if ($Mode -eq 'legacy-qd') {
            [IO.File]::WriteAllLines($capture,@('.echo EXEC_BP_LEGACY_QD','qd'),[Text.Encoding]::ASCII)
            $captureForCdb = $capture.Replace('\','\\')
            $moduleScript = Join-Path $run 'on-module.wds'
            [IO.File]::WriteAllLines($moduleScript,@(('ba e 1 /1 ProbeTarget!ProbeFunction "$<' + $captureForCdb + '"'),'g'),[Text.Encoding]::ASCII)
            $moduleForCdb = $moduleScript.Replace('\','\\')
            $commands.Add('sxe -c "$<' + $moduleForCdb + '" ld:ProbeModule')
            $commands.Add('g')
        } elseif ($Mode -eq 'handled-detach') {
            [IO.File]::WriteAllLines($capture,@('.echo EXEC_BP_HANDLED','.detach /h','q'),[Text.Encoding]::ASCII)
            $captureForCdb = $capture.Replace('\','\\')
            $moduleScript = Join-Path $run 'on-module.wds'
            [IO.File]::WriteAllLines($moduleScript,@(('ba e 1 /1 ProbeTarget!ProbeFunction "$<' + $captureForCdb + '"'),'g'),[Text.Encoding]::ASCII)
            $moduleForCdb = $moduleScript.Replace('\','\\')
            $commands.Add('sxe -c "$<' + $moduleForCdb + '" ld:ProbeModule')
            $commands.Add('g')
        } else {
            if ($Mode -eq 'cancel') {
                # User cancellation before arming is represented by a process
                # that skips the probe function and reaches a normal event.
                $commands.Add('sxe -c "qd" ld:ProbeDetach')
                $commands.Add('g')
            } else {
                # Validation failure occurs at the initial CDB break, before
                # the execution breakpoint is installed.
                $commands.Add('.echo VALIDATION_REJECTED_BEFORE_ARM')
                $commands.Add('.detach /h')
                $commands.Add('q')
            }
        }
        [IO.File]::WriteAllLines($boot,$commands,[Text.Encoding]::ASCII)

        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $cdb
        $start.Arguments = '-cf "' + $boot + '" "' + (Join-Path $run 'ProbeTarget.exe') + '"'
        $start.WorkingDirectory = $run
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $debugger = [Diagnostics.Process]::Start($start)
        $stdout = $debugger.StandardOutput.ReadToEndAsync()
        $stderr = $debugger.StandardError.ReadToEndAsync()

        $ready = Join-Path $run 'ready.txt'
        $readyDeadline = (Get-Date).AddSeconds(8)
        while (-not (Test-Path -LiteralPath $ready) -and (Get-Date) -lt $readyDeadline) { Start-Sleep -Milliseconds 30 }
        if (-not (Test-Path -LiteralPath $ready)) {
            if (-not $debugger.HasExited) { $debugger.Kill(); $debugger.WaitForExit() }
            throw "$Mode não iniciou o processo descartável; log: $log"
        }
        $targetPid = [int](Get-Content -LiteralPath $ready -Raw)
        $target = [Diagnostics.Process]::GetProcessById($targetPid)
        $targetHandle = $target.Handle
        try {
            if (-not $debugger.WaitForExit(15000)) {
                $debugger.Kill()
                $debugger.WaitForExit()
                if (-not $target.HasExited) { $target.Kill(); $target.WaitForExit() }
                throw "$Mode deixou a sessão CDB parada; log: $log"
            }
            if (-not $target.WaitForExit(5000)) {
                $target.Kill(); $target.WaitForExit()
                throw "$Mode deixou o processo de teste vivo após o término do CDB; log: $log"
            }
            $target.Refresh()
            $nativeTargetExitCode = [GrandMpDetachProbe.NativeProcess]::GetExitCode($targetHandle)
            if ($nativeTargetExitCode -eq 259) { throw "$Mode ainda está ativo segundo GetExitCodeProcess após WaitForExit." }
            $output = $stdout.Result + "`n" + $stderr.Result
            $logText = if (Test-Path -LiteralPath $log) { [IO.File]::ReadAllText($log) } else { '' }
            return [pscustomobject]@{
                Mode = $Mode
                DebuggerExitCode = $debugger.ExitCode
                TargetHasExited = $target.HasExited
                TargetExitCode = $nativeTargetExitCode
                Log = $logText
                Output = $output
                Done = Test-Path (Join-Path $run 'done.txt')
                NormalReturn = (Test-Path (Join-Path $run 'normal-return.txt')) -and ((Get-Content (Join-Path $run 'normal-return.txt') -Raw) -eq '0')
                AfterFunction = Test-Path (Join-Path $run 'after-function.txt')
                AfterDetachFunction = Test-Path (Join-Path $run 'after-detach-function.txt')
                Detached = (Test-Path (Join-Path $run 'after-detach.txt')) -and ((Get-Content (Join-Path $run 'after-detach.txt') -Raw) -eq 'detached')
                UnhandledException = if (Test-Path (Join-Path $run 'unhandled-exception.txt')) { Get-Content (Join-Path $run 'unhandled-exception.txt') -Raw } else { $null }
                Cancelled = Test-Path (Join-Path $run 'cancelled.txt')
                ValidationFailed = Test-Path (Join-Path $run 'validation-failed.txt')
            }
        }
        finally {
            [GrandMpDetachProbe.NativeProcess]::Close($targetHandle)
            $target.Dispose()
            if (-not $debugger.HasExited) { $debugger.Kill(); $debugger.WaitForExit() }
            $debugger.Dispose()
        }
    }

    $legacy = Invoke-Probe -Name 'legacy-qd' -Mode 'legacy-qd'
    if ($legacy.UnhandledException -ne '2147483652' -or $legacy.Done -or $legacy.AfterFunction) {
        throw ('Fluxo legado não reproduziu STATUS_SINGLE_STEP (0x80000004) no ponto de execução. Exceção={0}; Done={1}; AfterFunction={2}; log={3}' -f $legacy.UnhandledException,$legacy.Done,$legacy.AfterFunction,(Join-Path $testRoot 'legacy-qd\cdb.log'))
    }

    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $safe = Invoke-Probe -Name "handled-$attempt" -Mode 'handled-detach'
        if ($safe.DebuggerExitCode -ne 0 -or $safe.TargetExitCode -ne 0 -or -not $safe.Done -or -not $safe.NormalReturn -or -not $safe.AfterFunction -or -not $safe.AfterDetachFunction -or -not $safe.Detached -or $safe.UnhandledException -or $safe.Log -notmatch 'EXEC_BP_HANDLED' -or $safe.Log -notmatch 'quit:') {
            throw "Fluxo tratado/detach falhou na repetição $attempt (CDB=$($safe.DebuggerExitCode), targetExited=$($safe.TargetHasExited), target=$($safe.TargetExitCode), done=$($safe.Done), normal=$($safe.NormalReturn), after=$($safe.AfterFunction), afterDetachFunction=$($safe.AfterDetachFunction), detached=$($safe.Detached), unhandled=$($safe.UnhandledException), callback=$($safe.Log -match 'EXEC_BP_HANDLED'), quit=$($safe.Log -match 'quit:')). Confira $testRoot\handled-$attempt\cdb.log"
        }
    }

    $cancel = Invoke-Probe -Name 'cancelled' -Mode 'cancel'
    if ($cancel.DebuggerExitCode -ne 0 -or $cancel.TargetExitCode -ne 0 -or -not $cancel.Done -or -not $cancel.NormalReturn -or -not $cancel.Detached -or -not $cancel.Cancelled -or $cancel.UnhandledException) {
        throw "Caminho de cancelamento não concluiu/soltou o fixture: $testRoot\cancelled\cdb.log"
    }
    $failure = Invoke-Probe -Name 'validation-failure' -Mode 'validation-failure'
    if ($failure.DebuggerExitCode -ne 0 -or $failure.TargetExitCode -ne 0 -or -not $failure.Done -or -not $failure.NormalReturn -or -not $failure.Detached -or -not $failure.ValidationFailed -or $failure.UnhandledException -or $failure.Log -notmatch 'VALIDATION_REJECTED_BEFORE_ARM') {
        throw "Caminho de falha de validação não concluiu/soltou o fixture: $testRoot\validation-failure\cdb.log"
    }

    Write-Output 'CDB Win32 descartável: callback aninhado legacy qd reproduziu 0x80000004; 3/3 fluxos .detach /h + q destacaram o alvo e concluíram normalmente; cancelamento 1/1; falha de validação 1/1.'
}
finally {
    # Keep the logs under build/runtime-evidence-detach-tests for reproducibility.
    Write-Output "Logs do probe: $testRoot"
}
