[CmdletBinding()]
param([string]$RealCaptureDirectory)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. (Join-Path $repoRoot 'scripts\LoaderLockEvidence.ps1')
$fixture = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures\loader-lock-20261013-002726.txt'))
$realLog = Join-Path $repoRoot 'build\runtime-evidence\20261013-002726\01-loader-lock.txt'
if ($RealCaptureDirectory) { $realLog = Join-Path $RealCaptureDirectory '01-loader-lock.txt' }
$script:Cases = 0

function Test-Result {
    param([string]$Name, [string]$Text, [string]$Status = 'INCONCLUSIVO', [string]$ReasonCode)
    $result = Get-LoaderLockEvidenceResult -LogText $Text
    if ($result.Status -ne $Status -or $result.ReasonCode -ne $ReasonCode) {
        throw "$Name : esperado $Status/$ReasonCode, recebido $($result.Status)/$($result.ReasonCode): $($result.Reason)"
    }
    if ($Status -eq 'OBSERVADO_LIVRE') {
        foreach ($expected in @{
            ProcessId=0x4c80; ThreadId=0x5b04; InstructionPointer=0x715445f0; EntryAddress=0x715445f0
            Peb=0x00291000; FieldAddress=0x002910a0; ObjectAddress=0x779d6354
            LockCount=-1; RecursionCount=0; OwnerThread=0; ParserVersion=2; SymbolAge=1
        }.GetEnumerator()) {
            if ($result.($expected.Key) -ne $expected.Value) { throw "$Name : $($expected.Key) incorreto." }
        }
        if ($result.SymbolGuid -ne '7C1F7D48-576A-AE47-EB48-E43D5FB752AD') { throw "$Name : GUID incorreto." }
    }
    $script:Cases++
    Write-Host "PASS $Name : $Status/$ReasonCode"
}

if (Test-Path -LiteralPath $realLog) {
    Test-Result -Name 'log físico integral 20261013-002726' -Text ([IO.File]::ReadAllText($realLog)) -Status 'OBSERVADO_LIVRE' -ReasonCode 'FREE_SNAPSHOT'
} elseif ($RealCaptureDirectory) { throw 'Log real indicado não encontrado.' }
Test-Result -Name 'fixture extraído do log físico' -Text $fixture -Status 'OBSERVADO_LIVRE' -ReasonCode 'FREE_SNAPSHOT'
$originalContext = [regex]::Match($fixture, '(?ms)^LL_CONTEXT pid=.*?(?=^0:000> \|)').Value
if (-not $originalContext) { throw 'Contexto do fixture não encontrado.' }
$oneLine = "LL_CONTEXT pid=4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n"
$clean = $fixture.Replace($originalContext, $oneLine)
Test-Result -Name 'formato antigo numa linha' -Text $clean -Status 'OBSERVADO_LIVRE' -ReasonCode 'FREE_SNAPSHOT'
$noiseAtSeveralFields = @'
LL_CONTEXT pid=
SYMSRV: BYINDEX: 0x7
         symbol.pdb
         11223344
4c80 tid=
DBGHELP: loading symbols
         C:\symbols\module.pdb
5b04 eip=
SYMSRV: RESULT: 0x00000000
715445f0 entry=715445f0 peb=00291000
'@
Test-Result -Name 'diagnósticos entre vários campos' -Text ($fixture.Replace($originalContext, $noiseAtSeveralFields + "`r`n")) -Status 'OBSERVADO_LIVRE' -ReasonCode 'FREE_SNAPSHOT'
$held = $clean.Replace('LockCount        : 0n-1','LockCount        : 0n-2').Replace('RecursionCount   : 0n0','RecursionCount   : 0n1').Replace('OwningThread     : (null)','OwningThread     : 0x5b04 Void')
Test-Result -Name 'lock adquirido pela thread atual' -Text $held -Status 'OBSERVADO_OCUPADO' -ReasonCode 'OWNED_SNAPSHOT'
if (-not (Get-LoaderLockEvidenceResult -LogText $held).OwnerIsCurrentThread) { throw 'Owner não identificado como TID atual.' }

$negative = @(
    @{ Name='log vazio'; Text=''; Code='CAPTURE_INCOMPLETE' },
    @{ Name='início ausente'; Text=($clean.Replace("LL_CAPTURE_BEGIN`r`n",'')); Code='CAPTURE_INCOMPLETE' },
    @{ Name='fim ausente'; Text=($clean.Replace("LL_CAPTURE_DONE`r`n",'')); Code='CAPTURE_INCOMPLETE' },
    @{ Name='capturas concatenadas'; Text=($clean + $clean); Code='CAPTURE_INCOMPLETE' },
    @{ Name='detach ausente'; Text=($clean.Replace("Detached`r`n",'')); Code='DETACH_UNCONFIRMED' },
    @{ Name='detach repetido'; Text=($clean + "`r`nDetached`r`n"); Code='DETACH_UNCONFIRMED' },
    @{ Name='detach anterior à captura'; Text=("Detached`r`n" + $clean.Replace("Detached`r`n",'')); Code='DETACH_UNCONFIRMED' },
    @{ Name='contexto ausente'; Text=($fixture.Replace($originalContext,'')); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='contexto repetido'; Text=($fixture.Replace($originalContext,$oneLine+$oneLine)); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='campo de contexto ausente'; Text=($clean.Replace('tid=5b04 ','')); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='campo de contexto duplicado'; Text=($clean.Replace('tid=5b04','tid=5b04 tid=5b04')); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='ruído desconhecido'; Text=($fixture.Replace($originalContext,"LL_CONTEXT pid=`r`nUNKNOWN: 4c80`r`n4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n")); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='limite de linhas de contexto'; Text=($fixture.Replace($originalContext, ("LL_CONTEXT pid=`r`nSYMSRV: loading`r`n" + ("         continuation`r`n" * 65) + "4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n"))); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='limite de tamanho de contexto'; Text=($fixture.Replace($originalContext, ("LL_CONTEXT pid=`r`nSYMSRV: " + ('x' * 8192) + "`r`n4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n"))); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='valores somente em diagnóstico'; Text=($fixture.Replace($originalContext,"LL_CONTEXT pid=`r`nSYMSRV: 4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n")); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='continuação diagnóstica ambígua'; Text=($fixture.Replace($originalContext,"LL_CONTEXT pid=`r`nDBGHELP: loading symbols`r`n   4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n")); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='campos após outro prompt'; Text=($fixture.Replace($originalContext,"LL_CONTEXT pid=`r`n0:000> .echo unrelated`r`n4c80 tid=5b04 eip=715445f0 entry=715445f0 peb=00291000`r`n")); Code='CONTEXT_EXTRACTION_FAILED' },
    @{ Name='EIP divergente da entrada'; Text=($clean.Replace('eip=715445f0 entry=','eip=715445f1 entry=')); Code='ENTRY_MISMATCH' },
    @{ Name='TID contradiz thread atual'; Text=($clean.Replace('tid=5b04','tid=5b05')); Code='CONTEXT_CORROBORATION_FAILED' },
    @{ Name='EIP contradiz registradores'; Text=($clean.Replace('eip=715445f0 esp=','eip=715445f1 esp=')); Code='CONTEXT_CORROBORATION_FAILED' },
    @{ Name='PDB não carregado'; Text=($clean.Replace('Symbol Type: PDB','Symbol Type: EXPORT')); Code='NTDLL_SYMBOLS_UNCONFIRMED' },
    @{ Name='símbolos exatos ausentes'; Text=($clean.Replace('SYMOPT_EXACT_SYMBOLS','SYMOPT_UNCONFIRMED')); Code='NTDLL_SYMBOLS_UNCONFIRMED' },
    @{ Name='tipo Void'; Text=($clean.Replace('_RTL_CRITICAL_SECTION','Void')); Code='FIELD_TYPE_UNCONFIRMED' },
    @{ Name='seção de campo repetida'; Text=($clean.Replace("LL_FIELD_BEGIN`r`n","LL_FIELD_BEGIN`r`nLL_FIELD_BEGIN`r`n")); Code='FIELD_TYPE_UNCONFIRMED' },
    @{ Name='endereço do campo divergente'; Text=($clean.Replace('** 0x002910a0','** 0x002910b0')); Code='FIELD_ADDRESS_UNCONFIRMED' },
    @{ Name='objeto divergente do ponteiro do campo'; Text=($clean.Replace('* 0x779d6354','* 0x779d6358')); Code='OBJECT_POINTER_UNCONFIRMED' },
    @{ Name='leitura de objeto inválida'; Text=($clean.Replace("LL_OBJECT_END`r`n","Memory access error`r`nLL_OBJECT_END`r`n")); Code='OBJECT_POINTER_UNCONFIRMED' },
    @{ Name='owner ausente'; Text=([regex]::Replace($clean,'(?m)^.*OwningThread.*\r?\n','')); Code='CRITICAL_SECTION_DATA_UNCONFIRMED' },
    @{ Name='contagem duplicada'; Text=($clean.Replace('   +0x004 LockCount        : 0n-1',"   +0x004 LockCount        : 0n-1`r`n   +0x004 LockCount        : 0n-1")); Code='CRITICAL_SECTION_DATA_UNCONFIRMED' },
    @{ Name='contagem fora do tipo'; Text=($clean.Replace('LockCount        : 0n-1','LockCount        : 0n4294967295')); Code='CRITICAL_SECTION_DATA_UNCONFIRMED' },
    @{ Name='owner/recursão contraditórios'; Text=($clean.Replace('RecursionCount   : 0n0','RecursionCount   : 0n1')); Code='CRITICAL_SECTION_INCONSISTENT' },
    @{ Name='erro sintático'; Text=($clean + "`r`nSyntax error`r`n"); Code='CDB_SCRIPT_ERROR' },
    @{ Name='implicit wait'; Text=($clean + "`r`nCommand file caused an implicit wait`r`n"); Code='CDB_SCRIPT_ERROR' }
)
foreach ($case in $negative) { Test-Result -Name $case.Name -Text $case.Text -ReasonCode $case.Code }

# Replay the previous real-CDB lab recordings without starting CDB again.
$lab = Join-Path $repoRoot 'build\runtime-evidence-loaderlock-tests\2ec9a4fc166f4bb38e26dd817c8c054d'
if (Test-Path -LiteralPath $lab) {
    foreach ($case in @{
        'free-1'='OBSERVADO_LIVRE'; 'free-2'='OBSERVADO_LIVRE'; 'free-3'='OBSERVADO_LIVRE'
        'held-in-dllmain'='OBSERVADO_OCUPADO'; 'missing-type'='INCONCLUSIVO'
        'syntax-error'='INCONCLUSIVO'; 'invalid-guard'='INCONCLUSIVO'; 'missing-entry'='INCONCLUSIVO'
    }.GetEnumerator()) {
        $recording = [IO.File]::ReadAllText((Join-Path $lab ($case.Key + '\01-loader-lock.txt')))
        if ((Get-LoaderLockEvidenceResult -LogText $recording).Status -ne $case.Value) { throw "Regressão no recording $($case.Key)." }
        $script:Cases++
        Write-Host "PASS replay offline $($case.Key) : $($case.Value)"
    }
}

# Exercise the real launcher's offline route. This creates report artifacts
# only; no CDB, target process query, executable or fixture is launched.
$evidenceRoot = Join-Path $repoRoot 'build\runtime-evidence'
$testRoot = Join-Path $evidenceRoot ('parser-regression-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$logPath = Join-Path $testRoot '01-loader-lock.txt'
$originalPath = Join-Path $testRoot 'loader-lock-result.json'
[IO.File]::WriteAllText($logPath, $fixture, [Text.UTF8Encoding]::new($true))
[IO.File]::WriteAllText($originalPath, '{"Status":"INCONCLUSIVO","Test":"original"}', [Text.UTF8Encoding]::new($true))
$logHash = (Get-FileHash $logPath).Hash
$originalHash = (Get-FileHash $originalPath).Hash
$launcher = Join-Path $repoRoot 'scripts\Invoke-RuntimeEvidence.ps1'
& $launcher -ReprocessDirectory $testRoot
$reports = @(Get-ChildItem $testRoot -Filter 'loader-lock-result.reprocessed-*.json')
if ($reports.Count -ne 1) { throw 'Launcher offline não criou exatamente um novo relatório.' }
$report = [IO.File]::ReadAllText($reports[0].FullName) | ConvertFrom-Json
if ($report.Status -ne 'OBSERVADO_LIVRE' -or $report.Audit.SourceLogSha256 -ne $logHash -or $report.Audit.OriginalResultSha256 -ne $originalHash -or
    $report.Audit.ParserSha256 -ne (Get-FileHash (Join-Path $repoRoot 'scripts\LoaderLockEvidence.ps1')).Hash) { throw 'Reprocessamento/auditoria incorretos.' }
$second = Export-LoaderLockReprocessedResult -CaptureDirectory $testRoot -EvidenceRoot $evidenceRoot
if ($second.Path -eq $reports[0].FullName -or (Get-FileHash $logPath).Hash -ne $logHash -or (Get-FileHash $originalPath).Hash -ne $originalHash) { throw 'Reprocessamento sobrescreveu evidência original.' }
$script:Cases++
Write-Host 'PASS reprocessamento offline: dois relatórios únicos, hashes auditáveis, originais inalterados.'

$denied = $false
try { $null = Export-LoaderLockReprocessedResult -CaptureDirectory $PSScriptRoot -EvidenceRoot $evidenceRoot } catch { $denied = $true }
if (-not $denied) { throw 'Reprocessamento fora de build/runtime-evidence foi aceito.' }
$script:Cases++
$denied = $false
try { & $launcher -ReprocessDirectory $testRoot -LoaderLockOnly } catch [System.Management.Automation.ParameterBindingException] { $denied = $true }
if (-not $denied) { throw 'Reprocessamento permitiu combinar modo que inicia GTA.' }
$script:Cases++
Write-Output "LoaderLock parser offline: $script:Cases/$script:Cases PASS. Artefatos: $testRoot"
