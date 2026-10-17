$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
$vsDevCmd = 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
if (-not (Test-Path -LiteralPath $cdb -PathType Leaf)) {
    Write-Output 'CDB validation fail-closed: SKIP (CDB x86 não instalado).'
    exit 0
}
if (-not (Test-Path -LiteralPath $vsDevCmd -PathType Leaf)) {
    throw 'CDB x86 existe, mas VsDevCmd.bat não foi encontrado; não é possível compilar o fixture Win32.'
}

$testRoot = Join-Path $repoRoot ('build\runtime-validation-tests\' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot -Force | Out-Null
try {
    $source = Join-Path $PSScriptRoot 'cdb-detach\ValidationTarget.cpp'
    Copy-Item -LiteralPath $source -Destination $testRoot
    $compile = '"' + $vsDevCmd + '" -no_logo -arch=x86 -host_arch=x64 && cd /d "' + $testRoot + '" && cl.exe /nologo /EHsc /MD /Od /Zi /Fe:ValidationTarget.exe ValidationTarget.cpp /link /DEBUG:FULL'
    & $env:ComSpec /d /s /c $compile
    if ($LASTEXITCODE -ne 0) { throw "Compilação do fixture Win32 falhou: exit $LASTEXITCODE." }

    $launcherPath = Join-Path $repoRoot 'scripts\Invoke-RuntimeEvidence.ps1'
    $tokens = $null
    $parseErrors = $null
    $launcherAst = [System.Management.Automation.Language.Parser]::ParseFile($launcherPath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -gt 0) { throw "Erro de sintaxe no launcher: $($parseErrors[0].Message)" }
    $generatorAst = $launcherAst.Find({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Write-DebuggerScripts' }, $true)
    if (-not $generatorAst) { throw 'Write-DebuggerScripts não encontrado.' }
    $script:RepoRoot = $repoRoot
    Invoke-Expression $generatorAst.Extent.Text

    $generatedDirectory = Join-Path $testRoot 'generated'
    New-Item -ItemType Directory -Path $generatedDirectory | Out-Null
    Write-DebuggerScripts -RunDirectory $generatedDirectory
    $generatedBoot = [IO.File]::ReadAllLines((Join-Path $generatedDirectory 'boot-menu.wds'))
    $guardLines = @($generatedBoot | Where-Object { $_ -match '^\.if\s' })
    if ($guardLines.Count -ne 1) { throw 'boot-menu deve conter exatamente uma linha de validação .if.' }
    $guard = $guardLines[0]
    if ($guard -match "`r|`n") { throw 'A condição CDB foi dividida em múltiplas linhas físicas.' }
    $guard = $guard.Replace('ba e 1 /1 @$t0', 'ba e 1 /1 ValidationTarget!ProbeFunction')
    # Use a literal callback command in this disposable test. The production
    # nested $< callback path has its own real-CDB coverage in the script audit.
    $guard = [regex]::Replace($guard, '"\$<[^"\r\n]+"', '".echo HW_BREAKPOINT_CALLBACK_RAN; .detach /h; q"')
    if ($guard -match 'capture-menu\.wds|\$<') { throw 'Callback gerado não foi substituído pelo callback in-process do fixture.' }

    function Invoke-GateCase {
        param([Parameter(Mandatory)][string]$Name, [string]$TargetArgument, [switch]$CorruptSyntax, [switch]$InvalidPointer)
        $run = Join-Path $testRoot $Name
        New-Item -ItemType Directory -Path $run | Out-Null
        Copy-Item (Join-Path $testRoot 'ValidationTarget.exe'),(Join-Path $testRoot 'ValidationTarget.pdb') -Destination $run
        if ($TargetArgument) { $argumentLine = ' ' + $TargetArgument } else { $argumentLine = '' }
        $caseGuard = $guard
        if ($CorruptSyntax) { $caseGuard = '.if (((dwo(@$t0) == 0xffd55ae8) { .echo MUST_NOT_RUN; ba e 1 /1 ValidationTarget!ProbeFunction; g }' }
        $setT0 = 'r @$t0 = ValidationTarget!GateBytes'
        if ($InvalidPointer) { $setT0 = 'r @$t0 = 0xdeadbeef' }
        $bootLines = [System.Collections.Generic.List[string]]::new()
        $bootLines.Add('.logopen ' + (Join-Path $run 'cdb.log'))
        $bootLines.Add('.sympath+ ' + $run)
        $bootLines.Add('.reload /f ValidationTarget.exe')
        $bootLines.Add('sxe -c "q" epr')
        $bootLines.Add('bu ValidationTarget!ValidationPoint')
        $bootLines.Add('g')
        $bootLines.Add($setT0)
        $bootLines.Add('r @$t1 = ValidationTarget!TargetBytes')
        $bootLines.Add($caseGuard)
        $bootLines.Add('.logclose')
        $bootLines.Add('.detach /h')
        $bootLines.Add('q')
        $boot = Join-Path $run 'boot.wds'
        [IO.File]::WriteAllText($boot, ($bootLines -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)

        $start = [Diagnostics.ProcessStartInfo]::new()
        $start.FileName = $cdb
        $start.Arguments = '-cf "' + $boot + '" "' + (Join-Path $run 'ValidationTarget.exe') + '"' + $argumentLine
        $start.WorkingDirectory = $run
        $start.UseShellExecute = $false
        $start.CreateNoWindow = $true
        $start.RedirectStandardOutput = $true
        $start.RedirectStandardError = $true
        $debugger = [Diagnostics.Process]::Start($start)
        $stdout = $debugger.StandardOutput.ReadToEndAsync()
        $stderr = $debugger.StandardError.ReadToEndAsync()
        try {
            if (-not $debugger.WaitForExit(12000)) {
                $debugger.Kill()
                $debugger.WaitForExit()
                throw "$Name deixou o CDB pendurado; log: $(Join-Path $run 'cdb.log')"
            }
            $readyDeadline = (Get-Date).AddSeconds(3)
            while (-not (Test-Path -LiteralPath (Join-Path $run 'done.txt')) -and (Get-Date) -lt $readyDeadline) { Start-Sleep -Milliseconds 25 }
            $output = [string]::Concat($stdout.Result, "`n", $stderr.Result)
            $logPath = Join-Path $run 'cdb.log'
            $log = if (Test-Path -LiteralPath $logPath) { [IO.File]::ReadAllText($logPath) } else { '' }
            $done = Test-Path -LiteralPath (Join-Path $run 'done.txt')
            $validated = $log -match '(?m)^VALIDATION_PASSED\s*$'
            $breakpointCallback = $log -match '(?m)^HW_BREAKPOINT_CALLBACK_RAN\s*$'
            $mustNotRun = $log -match '(?m)^MUST_NOT_RUN\s*$'
            $failClosed = $log -match '(?m)^FAIL_CLOSED\s*$'
            $diagnosticText = $output + $log
            $memoryAccessError = $diagnosticText -match '(?i)Memory access error'
            $syntaxError = $diagnosticText -match '(?i)Syntax error|error CXX|Bad register|Could not read'
            return [pscustomobject]@{ Name=$Name; ExitCode=$debugger.ExitCode; Done=$done; Validated=$validated; FailClosed=$failClosed; Callback=$breakpointCallback; MustNotRun=$mustNotRun; SyntaxError=$syntaxError; MemoryAccessError=$memoryAccessError; Log=$log; Output=$output; Run=$run }
        }
        finally {
            if (-not $debugger.HasExited) { $debugger.Kill(); $debugger.WaitForExit() }
            $debugger.Dispose()
        }
    }

    $positive = Invoke-GateCase -Name 'valid'
    if ($positive.ExitCode -ne 0 -or -not $positive.Done -or -not $positive.Validated -or -not $positive.Callback -or $positive.MustNotRun -or $positive.SyntaxError) {
        throw "Caso válido não armou/disparou o callback corretamente: $($positive.Run)\cdb.log`n$($positive.Output)`n$($positive.Log)"
    }

    foreach ($case in @(
        @{ Name='bad-call-bytes'; Argument='bad-call' },
        @{ Name='bad-target-bytes'; Argument='bad-target' },
        @{ Name='invalid-read'; InvalidPointer=$true }
    )) {
        $caseArgument = if ($case.ContainsKey('Argument')) { $case.Argument } else { '' }
        $caseInvalidPointer = $case.ContainsKey('InvalidPointer') -and $case.InvalidPointer
        $result = Invoke-GateCase -Name $case.Name -TargetArgument $caseArgument -InvalidPointer:$caseInvalidPointer
        $expectedEvaluatorError = $caseInvalidPointer
        $hasUnexpectedError = if ($caseInvalidPointer) {
            (-not $result.MemoryAccessError) -or $result.SyntaxError
        } else {
            $result.MemoryAccessError -or $result.SyntaxError
        }
        $missingFailClosedMarker = (-not $caseInvalidPointer -and -not $result.FailClosed)
        if ($result.ExitCode -ne 0 -or -not $result.Done -or $result.Validated -or $result.Callback -or $result.MustNotRun -or $missingFailClosedMarker -or $hasUnexpectedError) {
            throw "Condição inválida não falhou fechada em $($case.Name): $($result.Run)\cdb.log`n$($result.Output)`n$($result.Log)"
        }
    }

    $syntax = Invoke-GateCase -Name 'syntax-error' -CorruptSyntax
    if ($syntax.ExitCode -ne 0 -or -not $syntax.Done -or $syntax.Validated -or $syntax.Callback -or $syntax.MustNotRun -or -not $syntax.SyntaxError) {
        throw "Erro sintático não falhou fechada ou não foi observado: $($syntax.Run)\cdb.log`n$($syntax.Output)`n$($syntax.Log)"
    }

    $missed = Invoke-GateCase -Name 'callsite-not-hit' -TargetArgument 'skip-hit'
    if ($missed.ExitCode -ne 0 -or -not $missed.Done -or -not $missed.Validated -or $missed.Callback -or $missed.MustNotRun) {
        throw "Saída do alvo sem atingir o call-site não encerrou CDB corretamente: $($missed.Run)\cdb.log`n$($missed.Output)`n$($missed.Log)"
    }

    Write-Output 'CDB Win32 fail-closed: positivo 1/1; bytes incorretos 2/2, leitura inválida 1/1 e sintaxe inválida 1/1 não armaram breakpoint/callback; processo encerrado sem atingir call-site acionou epr e CDB terminou; sem hangs.'
    Write-Output "Logs preservados em: $testRoot"
}
catch {
    Write-Output "Logs preservados em: $testRoot"
    throw
}
