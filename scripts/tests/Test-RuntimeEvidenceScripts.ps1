$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$launcherPath = Join-Path $repoRoot 'scripts\Invoke-RuntimeEvidence.ps1'
$tokens = $null
$parseErrors = $null
$launcherAst = [System.Management.Automation.Language.Parser]::ParseFile($launcherPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "Erro de sintaxe PowerShell: $($parseErrors[0].Message)" }
$generatorAst = $launcherAst.Find({
    param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Write-DebuggerScripts'
}, $true)
if (-not $generatorAst) { throw 'Write-DebuggerScripts não encontrado no launcher.' }
$script:RepoRoot = $repoRoot
Invoke-Expression $generatorAst.Extent.Text

$runDirectory = Join-Path $repoRoot ('build\runtime-script-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
try {
    Write-DebuggerScripts -RunDirectory $runDirectory
    $expectedNames = @(
        'boot-initialize.wds', 'capture-grandmp-load.wds', 'capture-initialize.wds',
        'boot-menu.wds', 'capture-menu.wds', 'boot-gameplay.wds', 'capture-gameplay.wds'
    )
    $actualNames = @(Get-ChildItem -LiteralPath $runDirectory -Filter '*.wds' -File | ForEach-Object Name | Sort-Object)
    $sortedExpected = @($expectedNames | Sort-Object)
    if (Compare-Object $sortedExpected $actualNames) { throw 'O gerador não produziu exatamente os sete scripts esperados.' }

    $scripts = @{}
    foreach ($name in $expectedNames) {
        $path = Join-Path $runDirectory $name
        $content = [IO.File]::ReadAllText($path)
        if ($content.Contains([char]0)) { throw "NUL encontrado em $name." }
        $scripts[$name] = $content
    }

    foreach ($name in @('capture-initialize.wds', 'capture-menu.wds', 'capture-gameplay.wds')) {
        $lines = @($scripts[$name] -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        if ($lines.Count -lt 3 -or $lines[-3] -ne '.logclose' -or $lines[-2] -ne '.detach /h' -or $lines[-1] -ne 'q') {
            throw "$name deve fechar log, destacar com .detach /h e então sair com q. Final: $($lines[-3..-1] -join ' | ')"
        }
        if ($lines -contains 'qd') { throw "$name ainda usa qd durante o callback do hardware breakpoint." }
    }

    foreach ($name in @('boot-initialize.wds', 'boot-menu.wds', 'boot-gameplay.wds')) {
        $lines = @($scripts[$name] -split "`r?`n")
        $sympathLines = @($lines | Where-Object { $_ -match '^\s*\.sympath\+\s+' })
        if ($sympathLines.Count -ne 1) { throw "$name deve conter uma única diretiva .sympath+ em linha própria." }
        if ($sympathLines[0] -match ';\s*(?:sxe|\.echo|\.reload|\.logopen|g\b)') {
            throw "$name incorpora comandos WinDbg ao argumento de .sympath+."
        }
        $sympathIndex = [array]::IndexOf($lines, $sympathLines[0])
        $expectedNextCommand = if ($name -eq 'boot-initialize.wds') { '^sxe\b' } else { '^\.reload\b' }
        if ($sympathIndex -eq $lines.Count - 1 -or $lines[$sympathIndex + 1] -notmatch $expectedNextCommand) {
            throw "$name não separa .sympath+ do próximo comando em linhas distintas."
        }
        if (-not ($lines -contains '.symfix')) { throw "$name perdeu .symfix." }
        if (-not ($lines -contains 'sxe -c "q" epr')) { throw "$name não encerra CDB quando o processo-alvo termina sem atingir o breakpoint." }
    }

    foreach ($name in $expectedNames) {
        if ($scripts[$name] -match '(?im)^\s*(?:bp|bu)\b') { throw "$name contém breakpoint de software não permitido." }
    }
    $invokeLines = @(Select-String -LiteralPath $launcherPath -Pattern 'Invoke-Cdb -CdbPath \$cdb -Arguments')
    if ($invokeLines.Count -ne 3 -or @($invokeLines | Where-Object { $_.Line -match "'-pd'" }).Count -gt 0) {
        throw 'As três etapas precisam usar o detach explícito; -pd causa segundo detach após .detach /h.'
    }
    $expectedInitializeCallback = 'ba e 1 /1 GrandMP!InitializeASI "$<' + (Join-Path $runDirectory 'capture-initialize.wds').Replace('\', '\\') + '"'
    if ($scripts['capture-grandmp-load.wds'] -notmatch ('(?m)^' + [regex]::Escape($expectedInitializeCallback) + '$')) {
        throw 'O callback de InitializeASI não contém $< e o escape de caminho esperado.'
    }
    $expectedModuleCallback = 'sxe -c "$<' + (Join-Path $runDirectory 'capture-grandmp-load.wds').Replace('\', '\\') + '" ld:GrandMP'
    if (@($scripts['boot-initialize.wds'] -split "`r?`n" | Where-Object { $_ -ceq $expectedModuleCallback }).Count -ne 1) {
        throw 'sxe -c não contém $< com o caminho corretamente escapado e o seletor ld:GrandMP.'
    }
    foreach ($entry in @(
        @{ Name = 'boot-menu.wds'; Capture = 'capture-menu.wds' },
        @{ Name = 'boot-gameplay.wds'; Capture = 'capture-gameplay.wds' }
    )) {
        $lines = @($scripts[$entry.Name] -split "`r?`n" | ForEach-Object { $_.Trim() } | Where-Object { $_ })
        $guardLines = @($lines | Where-Object { $_ -match '^\.if\s' })
        if ($guardLines.Count -ne 1) { throw "$($entry.Name) precisa ter uma única condição fail-closed em uma linha." }
        $guard = $guardLines[0]
        foreach ($expected in @(
            'dwo(@$t0) == 0xffd55ae8', 'by(@$t0+4) == 0xff',
            'dwo(@$t1) == 0x530cec83', 'wo(@$t1+4) == 0x5756',
            '.echo VALIDATION_PASSED', 'ba e 1 /1 @$t0', '; bl; g } .else { .echo FAIL_CLOSED'
        )) {
            if (-not $guard.Contains($expected)) { throw "$($entry.Name) guard não contém a condição/ramificação esperada: $expected" }
        }
        $expectedCapture = '$<' + (Join-Path $runDirectory $entry.Capture).Replace('\', '\\')
        if (-not $guard.Contains($expectedCapture)) { throw "$($entry.Name) callback usa caminho inesperado ou sem escape." }
        if ($guard -match "`r|`n" -or ($guard.ToCharArray() | Where-Object { $_ -eq '{' }).Count -ne ($guard.ToCharArray() | Where-Object { $_ -eq '}' }).Count) {
            throw "$($entry.Name) conditionals não estão sintaticamente fechadas em uma única linha."
        }
        if ($lines[-3] -ne '.logclose' -or $lines[-2] -ne '.detach /h' -or $lines[-1] -ne 'q') {
            throw "$($entry.Name) não tem fallback fail-closed de detach após a validação."
        }
    }
    foreach ($name in $expectedNames) {
        if ($scripts[$name] -match '\$\$><') { throw "$name usa `$`$>< e condensaria linhas em separadores." }
    }

    # Semicolon is a valid symbol-path separator. This guards against rejecting
    # it on the directive line while the checks above catch appended commands.
    $semicolonPath = '.sympath+ srv*;C:\symbols-a;D:\symbols-b'
    if ($semicolonPath -notmatch '^\.sympath\+\s+srv\*;C:\\symbols-a;D:\\symbols-b$') {
        throw 'O teste local não preservou separadores legítimos do caminho de símbolos.'
    }

    $cdb = 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x86\cdb.exe'
    if (Test-Path -LiteralPath $cdb -PathType Leaf) {
        function Invoke-SxeCallbackProbe {
            param([string]$Name, [string]$ScriptToken, [bool]$EscapePath)

            $probeLog = Join-Path $runDirectory "$Name.log"
            $probeCapture = Join-Path $runDirectory "$Name-capture.wds"
            $probeBoot = Join-Path $runDirectory "$Name-boot.wds"
            $expectedMarker = "$Name-CALLBACK-RAN"
            [IO.File]::WriteAllText($probeCapture, ".echo $expectedMarker`r`nqd`r`n", [Text.Encoding]::ASCII)
            $capturePath = if ($EscapePath) { $probeCapture.Replace('\', '\\') } else { $probeCapture }
            $callback = '"' + $ScriptToken + $capturePath + '"'
            $bootText = ".logopen $probeLog`r`nsxe -c $callback epr`r`ng`r`n"
            [IO.File]::WriteAllText($probeBoot, $bootText, [Text.Encoding]::ASCII)

            $startInfo = [Diagnostics.ProcessStartInfo]::new()
            $startInfo.FileName = $cdb
            $startInfo.Arguments = "-cf $probeBoot $env:WINDIR\SysWOW64\cmd.exe /c exit 0"
            $startInfo.WorkingDirectory = $repoRoot
            $startInfo.UseShellExecute = $false
            $startInfo.CreateNoWindow = $true
            $startInfo.RedirectStandardOutput = $true
            $startInfo.RedirectStandardError = $true
            $process = [Diagnostics.Process]::Start($startInfo)
            try {
                $stdoutTask = $process.StandardOutput.ReadToEndAsync()
                $stderrTask = $process.StandardError.ReadToEndAsync()
                $completed = $process.WaitForExit(10000)
                if (-not $completed) { $process.Kill(); $process.WaitForExit(5000) | Out-Null }
                $output = [string]::Concat($stdoutTask.Result, "`n", $stderrTask.Result)
                $logText = if (Test-Path -LiteralPath $probeLog -PathType Leaf) { [IO.File]::ReadAllText($probeLog) } else { '' }
                return [pscustomobject]@{
                    Completed = $completed
                    ExitCode = if ($completed) { $process.ExitCode } else { $null }
                    Error123 = ($output -match 'Win32 error 0n123')
                    CallbackRan = $logText.Contains($expectedMarker)
                    Output = $output
                }
            }
            finally { $process.Dispose() }
        }

        $badSingleDollar = Invoke-SxeCallbackProbe -Name 'sxe-unescaped-path-control' -ScriptToken '$<' -EscapePath $false
        if (-not $badSingleDollar.Completed -or -not $badSingleDollar.Error123 -or $badSingleDollar.CallbackRan) {
            throw "O controle com caminho não escapado não reproduziu erro 123/ausência de callback como esperado; CDB pode ter ficado parado.`n$($badSingleDollar.Output)"
        }
        $badDoubleDollarRawPath = Invoke-SxeCallbackProbe -Name 'sxe-double-dollar-raw-path-control' -ScriptToken '$$<' -EscapePath $false
        if (-not $badDoubleDollarRawPath.Completed -or -not $badDoubleDollarRawPath.Error123 -or $badDoubleDollarRawPath.CallbackRan) {
            throw "$$< sem escapar as barras não reproduziu erro 123/ausência de callback como esperado.`n$($badDoubleDollarRawPath.Output)"
        }
        $goodSingleDollar = Invoke-SxeCallbackProbe -Name 'sxe-escaped-path' -ScriptToken '$<' -EscapePath $true
        if (-not $goodSingleDollar.Completed -or $goodSingleDollar.Error123 -or -not $goodSingleDollar.CallbackRan -or $goodSingleDollar.ExitCode -ne 0) {
            throw "O callback sxe -c com caminho escapado não terminou corretamente; possível sessão parada.`n$($goodSingleDollar.Output)"
        }
        Write-Output 'CDB x86 descartável: sxe -c reproduziu erro 123 com $< e $$< quando o caminho é cru; $< escapado executou callback, gravou log e encerrou sem sessão parada.'
    }
    else {
        Write-Output 'CDB x86 descartável: SKIP (CDB do Windows SDK não encontrado); auditoria estática executada.'
    }

    Write-Output 'Runtime evidence script audit: 7 scripts aprovados (linhas, .sympath, escapes, breakpoints e detach).'
}
finally {
    Remove-Item -LiteralPath $runDirectory -Recurse -Force
}
