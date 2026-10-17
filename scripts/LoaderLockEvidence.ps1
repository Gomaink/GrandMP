# Specific read-only InitializeASI evidence. No target-function calls or offsets.
function Write-LoaderLockDebuggerScripts {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory)][string]$RunDirectory,
        [Parameter(Mandatory)][string]$PdbDirectory,
        [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')][string]$EntryModule = 'GrandMP',
        [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*\.(asi|dll|exe)$')][string]$EntryImage = 'GrandMP.asi',
        [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')][string]$EntryName = 'InitializeASI',
        [ValidatePattern('^[A-Za-z][A-Za-z0-9_]*$')][string]$LoadModule = 'GrandMP'
    )

    if ($RunDirectory -match '[\s";]' -or $PdbDirectory -match '["\r\n]') {
        throw 'Caminho não suportado pelo script CDB; nenhum alvo iniciado.'
    }
    $entry = "${EntryModule}!${EntryName}"
    $log = Join-Path $RunDirectory '01-loader-lock.txt'
    $capturePath = (Join-Path $RunDirectory 'capture-loader-lock.wds').Replace('\', '\\')
    $modulePath = (Join-Path $RunDirectory 'capture-grandmp-load.wds').Replace('\', '\\')
    $failurePath = Join-Path $RunDirectory 'cleanup-inconclusive.wds'
    $cache = Join-Path (Split-Path -Parent $RunDirectory) 'symbols'

    # Single-quoted templates preserve every $ in CDB pseudo-registers and $<.
    # Typed expressions ask the PDB for the field and pointed-to type. In
    # particular there is NO cast of LoaderLock to RTL_CRITICAL_SECTION.
    $capture = @'
.echo LL_CAPTURE_BEGIN
.printf "LL_CONTEXT pid=%x tid=%x eip=%p entry=%p peb=%p\n", @$tpid, @$tid, @eip, @$t9, @$peb
|
~
r
kv 100
kb 100
dds @esp L80
lm
.echo LL_NTDLL_SYMBOLS_BEGIN
vertarget
lmvm ntdll
!lmi ntdll
.echo LL_NTDLL_SYMBOLS_END
.echo LL_FIELD_BEGIN
dt ntdll!_PEB @$peb LoaderLock
.echo LL_FIELD_END
.echo LL_FIELD_ADDRESS_BEGIN
?? &((ntdll!_PEB*)@$peb)->LoaderLock
.echo LL_FIELD_ADDRESS_END
.echo LL_OBJECT_BEGIN
?? ((ntdll!_PEB*)@$peb)->LoaderLock
.echo LL_OBJECT_END
.echo LL_CAPTURE_DONE
bc *
.detach /h
.logclose
q
'@
    $module = @'
.echo LL_MODULE_LOAD
lmv m __MODULE__
.reload /f __IMAGE__
!lmi __MODULE__
x __ENTRY__
r @$t9 = 0
r @$t9 = @@masm(__ENTRY__)
as ${/v:LL_CONTINUE} $<__FAILURE__
.if (@$t9 != 0) { ba e 1 /1 @$t9 "$<__CAPTURE__" } .else { .echo LL_ENTRY_UNRESOLVED }
r @$t8 = 0
r @$t8 = @@masm(@$bp0)
.if ((@$t8 != 0) & (@$t8 == @$t9)) { bl; as ${/v:LL_CONTINUE} g }
${LL_CONTINUE}
'@
    $failure = @'
.echo LL_INCONCLUSIVE_CLEANUP
bc *
.detach /h
.logclose
q
'@
    $boot = @'
.logopen __LOG__
.symfix __CACHE__
.sympath+ __PDB__
.symopt- 0x40
.symopt+ 0x400
!sym noisy
.reload /f ntdll.dll
sxe -c "$<__MODULE_SCRIPT__" ld:__LOAD_MODULE__
sxe -c ".echo LL_EXIT_BEFORE_CAPTURE; .logclose; q" epr
.echo LL_WAITING_FOR_MODULE_AND_ENTRY
g
'@
    $module = $module.Replace('__MODULE__', $EntryModule).Replace('__IMAGE__', $EntryImage).Replace('__ENTRY__', $entry).Replace('__CAPTURE__', $capturePath).Replace('__FAILURE__', $failurePath)
    $boot = $boot.Replace('__LOG__', $log).Replace('__CACHE__', $cache).Replace('__PDB__', $PdbDirectory).Replace('__MODULE_SCRIPT__', $modulePath).Replace('__LOAD_MODULE__', $LoadModule)
    foreach ($item in @(
        @{ Name='boot-initialize.wds'; Text=$boot },
        @{ Name='capture-grandmp-load.wds'; Text=$module },
        @{ Name='cleanup-inconclusive.wds'; Text=$failure },
        @{ Name='capture-loader-lock.wds'; Text=$capture }
    )) {
        $normalized = [regex]::Replace($item.Text.Trim(), '\r?\n', "`r`n") + "`r`n"
        [IO.File]::WriteAllText((Join-Path $RunDirectory $item.Name), $normalized, [Text.Encoding]::ASCII)
    }
}

function Get-LoaderLockEvidenceResult {
    [CmdletBinding()]
    param([Parameter(Mandatory)][AllowEmptyString()][string]$LogText)

    $result = [ordered]@{
        ParserVersion=2; Status='INCONCLUSIVO'; ReasonCode='CAPTURE_INCOMPLETE'; Reason='Captura incompleta: marcadores de início/fim ausentes ou repetidos.'
        ProcessId=$null; ThreadId=$null; InstructionPointer=$null; EntryAddress=$null; Peb=$null; FieldAddress=$null; ObjectAddress=$null
        OwnerThread=$null; RecursionCount=$null; LockCount=$null; OwnerIsCurrentThread=$null
        SymbolGuid=$null; SymbolAge=$null; ContextDiagnosticLinesRemoved=0; Scope='Snapshot na entrada; não autoriza integração com o motor.'
    }
    function Get-OneMatch([string]$Text, [string]$Pattern) {
        $found = [regex]::Matches($Text, $Pattern)
        if ($found.Count -eq 1) { return $found[0] }
        return $null
    }
    function Parse-Hex([string]$Value) { return [Convert]::ToUInt64($Value.Replace('`','').Replace('0x',''), 16) }

    if ($LogText -match '(?i)Syntax error|Command file (?:caused|execution failed)|HRESULT 0x80004005|Win32 error 0n123') {
        $result.ReasonCode = 'CDB_SCRIPT_ERROR'
        $result.Reason = 'Erro de interpretação/execução do script CDB; resultado não classificado.'
        return [pscustomobject]$result
    }

    $begin = Get-OneMatch $LogText '(?m)^LL_CAPTURE_BEGIN\r?$'
    $done = Get-OneMatch $LogText '(?m)^LL_CAPTURE_DONE\r?$'
    if (-not $begin -or -not $done -or $done.Index -le $begin.Index) { return [pscustomobject]$result }
    $detached = Get-OneMatch $LogText '(?m)^Detached\r?$'
    $result.ReasonCode = 'DETACH_UNCONFIRMED'
    $result.Reason = 'Detach não confirmado: confirmação ausente, repetida ou anterior ao fim da captura.'
    if (-not $detached -or $detached.Index -le $done.Index) { return [pscustomobject]$result }
    $capture = $LogText.Substring($begin.Index + $begin.Length, $done.Index - $begin.Index - $begin.Length)
    function Get-Section([string]$Name) {
        $start = Get-OneMatch $capture ('(?m)^LL_' + $Name + '_BEGIN\r?$')
        $end = Get-OneMatch $capture ('(?m)^LL_' + $Name + '_END\r?$')
        if ($start -and $end -and $start.Index -lt $end.Index) {
            return $capture.Substring($start.Index + $start.Length, $end.Index - $start.Index - $start.Length)
        }
        return ''
    }

    # Never search stacks/modules for substitute context values. Reconstruct
    # only this printf's output, bounded by the next debugger command prompt.
    $prefix = Get-OneMatch $capture '(?m)^LL_CONTEXT pid='
    $result.ReasonCode = 'CONTEXT_EXTRACTION_FAILED'
    $result.Reason = 'Falha na extração LL_CONTEXT: prefixo ausente/repetido ou campos incompletos/ambíguos.'
    if (-not $prefix) { return [pscustomobject]$result }
    $contextTail = $capture.Substring($prefix.Index)
    $nextPrompt = [regex]::Match($contextTail, '(?im)^(?:[0-9a-f]+:[0-9a-f]+>|NoTarget>)')
    if (-not $nextPrompt.Success) { return [pscustomobject]$result }
    $contextBlock = $contextTail.Substring(0, $nextPrompt.Index)
    $contextLines = @($contextBlock -split '\r?\n')
    if ([Text.Encoding]::UTF8.GetByteCount($contextBlock) -gt 8192 -or $contextLines.Count -gt 64) {
        $result.Reason = 'Falha na extração LL_CONTEXT: bloco excede 64 linhas ou 8 KiB de texto.'
        return [pscustomobject]$result
    }
    $fragments = [System.Collections.Generic.List[string]]::new()
    $inDiagnostic = $false
    foreach ($line in $contextLines) {
        if ($line -match '^(?:SYMSRV|DBGHELP):') {
            $inDiagnostic = $true
            $result.ContextDiagnosticLinesRemoved++
        } elseif ($line -match '^\*\*\* WARNING: Unable to verify (?:checksum|timestamp) for .+$') {
            $inDiagnostic = $false
            $result.ContextDiagnosticLinesRemoved++
        } elseif ($inDiagnostic -and $line -match '^[ \t]+\S') {
            # A field-like continuation cannot safely be treated as noise.
            if ($line -match '(?i)(?:LL_CONTEXT|\b(?:pid|tid|eip|entry|peb)\s*=)') { return [pscustomobject]$result }
            $result.ContextDiagnosticLinesRemoved++
        } elseif (-not [string]::IsNullOrWhiteSpace($line)) {
            $inDiagnostic = $false
            $fragments.Add($line.Trim())
        }
    }
    $hex = '([0-9a-f]{1,8})'
    $context = Get-OneMatch ($fragments -join ' ') ('(?i)^LL_CONTEXT pid=\s*' + $hex + '\s+tid=\s*' + $hex + '\s+eip=\s*' + $hex + '\s+entry=\s*' + $hex + '\s+peb=\s*' + $hex + '$')
    if (-not $context) { return [pscustomobject]$result }
    $result.ProcessId = Parse-Hex $context.Groups[1].Value
    $result.ThreadId = Parse-Hex $context.Groups[2].Value
    $result.InstructionPointer = Parse-Hex $context.Groups[3].Value
    $result.EntryAddress = Parse-Hex $context.Groups[4].Value
    $result.Peb = Parse-Hex $context.Groups[5].Value
    if ($result.ProcessId -eq 0 -or $result.ThreadId -eq 0 -or $result.Peb -eq 0) {
        $result.Reason = 'Falha na extração LL_CONTEXT: PID, TID ou PEB nulo.'
        return [pscustomobject]$result
    }
    if ($result.InstructionPointer -ne $result.EntryAddress -or $result.EntryAddress -eq 0) {
        $result.ReasonCode = 'ENTRY_MISMATCH'
        $result.Reason = 'EIP não corresponde à entrada esperada.'
        return [pscustomobject]$result
    }
    $thread = Get-OneMatch $capture '(?im)^\.[ \t]+[0-9]+[ \t]+Id:[ \t]+([0-9a-f]{1,8})\.([0-9a-f]{1,8})[ \t]'
    $register = Get-OneMatch $capture '(?im)^eip=([0-9a-f]{1,8})[ \t]'
    if (-not $thread -or -not $register -or
        (Parse-Hex $thread.Groups[1].Value) -ne $result.ProcessId -or
        (Parse-Hex $thread.Groups[2].Value) -ne $result.ThreadId -or
        (Parse-Hex $register.Groups[1].Value) -ne $result.InstructionPointer) {
        $result.ReasonCode = 'CONTEXT_CORROBORATION_FAILED'
        $result.Reason = 'Contexto não confirmado ou contradito pela thread atual (~) e registradores (r).'
        return [pscustomobject]$result
    }
    $symbols = Get-Section 'NTDLL_SYMBOLS'
    $guid = Get-OneMatch $symbols '(?i)RSDS - GUID:\s*\{([0-9A-F-]+)\}'
    $age = Get-OneMatch $symbols 'Age:\s*([0-9]{1,9}),\s*Pdb:'
    if ($symbols -notmatch 'Machine Type:\s*332\s*\(I386\)' -or
        $symbols -notmatch 'Symbol Type:\s*PDB\s*-\s*Symbols loaded successfully' -or
        $LogText -notmatch 'SYMOPT_EXACT_SYMBOLS' -or
        $symbols -match '(?i)mismatch|not match|could not|unable|error' -or -not $guid -or -not $age) {
        $result.ReasonCode = 'NTDLL_SYMBOLS_UNCONFIRMED'
        $result.Reason = 'PDB correspondente de ntdll x86 não confirmado.'
        return [pscustomobject]$result
    }
    $result.SymbolGuid = $guid.Groups[1].Value
    $result.SymbolAge = [int]$age.Groups[1].Value
    $field = Get-Section 'FIELD'
    $fieldMatch = Get-OneMatch $field '(?im)^[ \t]*\+0x([0-9a-f]{1,8})[ \t]+LoaderLock[ \t]*:[ \t]*0x([0-9a-f]{1,8})[ \t]+_RTL_CRITICAL_SECTION[ \t]*\r?$'
    if (-not $fieldMatch -or $field -match '(?i)error|unable|could not|\?\?\?') {
        $result.ReasonCode = 'FIELD_TYPE_UNCONFIRMED'
        $result.Reason = 'Campo LoaderLock ausente, repetido, sem tipo confiável ou com leitura inválida.'
        return [pscustomobject]$result
    }
    $result.FieldAddress = $result.Peb + (Parse-Hex $fieldMatch.Groups[1].Value)
    $address = Get-Section 'FIELD_ADDRESS'
    $storage = Get-OneMatch $address '(?im)^struct _RTL_CRITICAL_SECTION \*\* 0x([0-9a-f]{1,8})[ \t]*\r?$'
    if (-not $storage -or $address -match '(?i)error|unable|could not|\?\?\?' -or
        $result.FieldAddress -gt [uint32]::MaxValue -or $result.FieldAddress -ne (Parse-Hex $storage.Groups[1].Value)) {
        $result.ReasonCode = 'FIELD_ADDRESS_UNCONFIRMED'
        $result.Reason = 'Endereço do campo LoaderLock ausente, repetido ou divergente de PEB + offset obtido dos símbolos.'
        return [pscustomobject]$result
    }
    $object = Get-Section 'OBJECT'
    $pointer = Get-OneMatch $object '(?im)^struct _RTL_CRITICAL_SECTION \* 0x([0-9a-f]{1,8})[ \t]*\r?$'
    $result.ObjectAddress = Parse-Hex $fieldMatch.Groups[2].Value
    if (-not $pointer -or $object -match '(?i)error|unable|could not|\?\?\?' -or
        $result.ObjectAddress -eq 0 -or $result.ObjectAddress -ne (Parse-Hex $pointer.Groups[1].Value)) {
        $result.ReasonCode = 'OBJECT_POINTER_UNCONFIRMED'
        $result.Reason = 'Ponteiro/objeto LoaderLock ausente, repetido, nulo, divergente ou com leitura inválida.'
        return [pscustomobject]$result
    }
    $lock = Get-OneMatch $object '(?im)^[ \t]*\+0x[0-9a-f]+[ \t]+LockCount[ \t]*:[ \t]*0n(-?[0-9]{1,10})[ \t]*\r?$'
    $recursion = Get-OneMatch $object '(?im)^[ \t]*\+0x[0-9a-f]+[ \t]+RecursionCount[ \t]*:[ \t]*0n([0-9]{1,10})[ \t]*\r?$'
    $owner = Get-OneMatch $object '(?im)^[ \t]*\+0x[0-9a-f]+[ \t]+OwningThread[ \t]*:[ \t]*(\(null\)|0x[0-9a-f]{1,8}(?:[ \t]+Void)?)[ \t]*\r?$'
    if (-not $lock -or -not $recursion -or -not $owner) {
        $result.ReasonCode = 'CRITICAL_SECTION_DATA_UNCONFIRMED'
        $result.Reason = 'Owner/contagens ausentes, repetidos ou com formato não reconhecido.'
        return [pscustomobject]$result
    }
    $result.LockCount = [long]$lock.Groups[1].Value
    $result.RecursionCount = [long]$recursion.Groups[1].Value
    if ($result.LockCount -lt [int]::MinValue -or $result.LockCount -gt [int]::MaxValue -or $result.RecursionCount -gt [int]::MaxValue) {
        $result.ReasonCode = 'CRITICAL_SECTION_DATA_UNCONFIRMED'
        $result.Reason = 'Contagens fora da representação assinada indicada pelo tipo.'
        return [pscustomobject]$result
    }
    $result.OwnerThread = if ($owner.Groups[1].Value -eq '(null)') { [uint64]0 } else { Parse-Hex (($owner.Groups[1].Value -split '\s+')[0]) }
    $result.OwnerIsCurrentThread = $result.OwnerThread -eq $result.ThreadId
    if ($result.OwnerThread -eq 0 -and $result.RecursionCount -eq 0 -and $result.LockCount -eq -1) {
        $result.Status = 'OBSERVADO_LIVRE'
        $result.ReasonCode = 'FREE_SNAPSHOT'
        $result.Reason = 'Objeto tipado: owner nulo, RecursionCount=0, LockCount=-1 neste snapshot.'
    } elseif ($result.OwnerThread -ne 0 -and $result.RecursionCount -gt 0 -and $result.LockCount -ne -1) {
        $result.Status = 'OBSERVADO_OCUPADO'
        $result.ReasonCode = 'OWNED_SNAPSHOT'
        $result.Reason = 'Objeto tipado: owner e recursão não nulos neste snapshot.'
    } else {
        $result.ReasonCode = 'CRITICAL_SECTION_INCONSISTENT'
        $result.Reason = 'Contagens/owner não permitem interpretação inequívoca.'
    }
    return [pscustomobject]$result
}

function Export-LoaderLockReprocessedResult {
    [CmdletBinding()]
    param([Parameter(Mandatory)][string]$CaptureDirectory, [Parameter(Mandatory)][string]$EvidenceRoot)

    $root = [IO.Path]::GetFullPath($EvidenceRoot).TrimEnd([IO.Path]::DirectorySeparatorChar)
    $directory = [IO.Path]::GetFullPath($CaptureDirectory)
    if (-not $directory.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Reprocessamento aceita somente subdiretórios de build/runtime-evidence.'
    }
    $item = Get-Item -LiteralPath $directory -Force
    if (-not $item.PSIsContainer) { throw 'A captura deve ser um diretório existente.' }
    while ($item) {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Reparse point não permitido no caminho da captura.' }
        $item = $item.Parent
    }
    $logPath = Join-Path $directory '01-loader-lock.txt'
    $originalPath = Join-Path $directory 'loader-lock-result.json'
    foreach ($path in @($logPath, $originalPath)) {
        if (Test-Path -LiteralPath $path) {
            $file = Get-Item -LiteralPath $path -Force
            if ($file.PSIsContainer -or ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'Arquivo de evidência deve ser regular, sem reparse point.' }
        }
    }
    function Get-BytesHash([byte[]]$Bytes) {
        $hash = [Security.Cryptography.SHA256]::Create()
        try { return [BitConverter]::ToString($hash.ComputeHash($Bytes)).Replace('-','') }
        finally { $hash.Dispose() }
    }
    $bytes = [IO.File]::ReadAllBytes($logPath)
    $stream = [IO.MemoryStream]::new($bytes, $false)
    $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true)
    try { $logText = $reader.ReadToEnd() }
    finally { $reader.Dispose() }
    $result = Get-LoaderLockEvidenceResult -LogText $logText
    $originalHash = if (Test-Path -LiteralPath $originalPath -PathType Leaf) { Get-BytesHash ([IO.File]::ReadAllBytes($originalPath)) } else { $null }
    $parserPath = $MyInvocation.MyCommand.ScriptBlock.File
    $result | Add-Member -NotePropertyName Audit -NotePropertyValue ([ordered]@{
        Mode='Offline reprocess; no debugger or target process'
        ReprocessedAtUtc=[DateTime]::UtcNow.ToString('o')
        SourceLog=$logPath; SourceLogSha256=(Get-BytesHash $bytes)
        OriginalResult=$originalPath; OriginalResultSha256=$originalHash
        ParserPath=$parserPath; ParserSha256=(Get-BytesHash ([IO.File]::ReadAllBytes($parserPath)))
    })
    $name = 'loader-lock-result.reprocessed-' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmssfff') + '-' + [guid]::NewGuid().ToString('N').Substring(0,8) + '.json'
    $output = Join-Path $directory $name
    $fileStream = [IO.File]::Open($output, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $writer = [IO.StreamWriter]::new($fileStream, [Text.UTF8Encoding]::new($true))
    try { $writer.WriteLine(($result | ConvertTo-Json -Depth 5)) }
    finally { $writer.Dispose() }
    return [pscustomobject]@{ Path=$output; Result=$result }
}
