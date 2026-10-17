# Alpha 0.4.1 — substituição pela variante com diagnósticos

**Somente após revisão/autorização do operador. Este procedimento não foi executado.** Build Debug/Win32 com o mecanismo existente (`GRANDMP_BUILD_PED_SMOKE_ENABLED=ON`, target `grandmp_ped_smoke_enabled`). Nenhuma alteração nos fontes/CMake/testes revisados. Build padrão continua bloqueada; outputs novos em `build/alpha041-spawn-physical-win32`, sem sobrescrever builds anteriores.

- ASI habilitada: `build/alpha041-spawn-physical-win32/client/windows/ped-smoke-enabled/Debug/GrandMPPedSmoke.asi`.
- SHA-256 habilitada: `9B666254F414F2244FDEAD94678D436538384DAF9D93222A19271BA752EFA43F`.
- SHA-256 padrão bloqueada: `FA96DBAFE7D70E2A0D5E9A0F049011B5252C51C94A9D456818F5E4AFB41CC4EB`.
- **26/26 testes: 13 padrão + 13 habilitados, 0,72 s; build sem warnings.** Logs `build/alpha041-spawn-physical/ped-build.txt` e `ped-ctest.txt`.
- PE32/MAP/desassemblagem confirmam diagnósticos de modelo/player/posição/right/solo/desnível/esfera e recusas específicas. Referências dos formatos conferidas nos métodos nativos vinculados. CALL original, SHA Hoodlum, thread/identidade, opt-in/F6, thunk e spawn único também conferidos. Evidência: `artifact-verification.json` e `artifact-gates-disasm.txt` no mesmo diretório.
- Seis ASIs anteriores conferidos por hash e timestamp antes/depois; lista em `previous-artifacts.json`. Nenhum arquivo de GrandMP-Smoke foi alterado; sem GTA, patch físico, commit/push.

## Substituição incremental, com GTA totalmente encerrado

O bloco só aceita a ASI experimental anterior de SHA `AE02EDF8F0C7AB9666AF29A8FC79BA2EA1778BFF8A1C0E1809F579DA2D341EFE`. Arquivo ausente/divergente ou backup prévio faz parar; não substituir por suposição. Copia backup/log anteriores para o workspace e troca somente `scripts/GrandMPPedSmoke.asi`. Outros ASIs são apenas lidos para conferir hash/timestamp.

```powershell
$ErrorActionPreference = 'Stop'
$pedRepo = 'D:\Programacao\Workspace\Applications\grandmp'
$pedGame = 'D:\Jogos\GrandMP-Smoke'
$pedEvidence = Join-Path $pedRepo 'build\alpha041-spawn-physical'
$pedSource = Join-Path $pedRepo 'build\alpha041-spawn-physical-win32\client\windows\ped-smoke-enabled\Debug\GrandMPPedSmoke.asi'
$pedTarget = Join-Path $pedGame 'scripts\GrandMPPedSmoke.asi'
$pedLog = Join-Path $pedGame 'scripts\GrandMPPedSmoke.log'
$pedBackup = Join-Path $pedEvidence 'GrandMPPedSmoke.previous.asi'
$pedLogBackup = Join-Path $pedEvidence 'GrandMPPedSmoke.previous.log'
$pedManifest = Join-Path $pedEvidence 'operator-other-asis-before.json'
$pedOldSha = 'AE02EDF8F0C7AB9666AF29A8FC79BA2EA1778BFF8A1C0E1809F579DA2D341EFE'
$pedNewSha = '9B666254F414F2244FDEAD94678D436538384DAF9D93222A19271BA752EFA43F'
function Assert-PedGtaClosed {
    if (Get-Process -Name 'gta_sa' -ErrorAction SilentlyContinue) { throw 'Encerre GTA antes de alterar arquivos.' }
}
function Get-PedOtherAsis {
    Get-ChildItem -LiteralPath $pedGame -Filter '*.asi' -File -Recurse |
        Where-Object { $_.FullName -ne $pedTarget } | Sort-Object FullName | ForEach-Object {
            [pscustomobject]@{ Path=$_.FullName; Sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash; LastWriteTicks=$_.LastWriteTimeUtc.Ticks }
        }
}
Assert-PedGtaClosed
if (!(Test-Path -LiteralPath $pedTarget -PathType Leaf)) { throw 'ASI anterior ausente; parar.' }
if ((Get-FileHash -LiteralPath $pedSource -Algorithm SHA256).Hash -ne $pedNewSha) { throw 'Nova ASI divergente.' }
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedOldSha) { throw 'ASI instalada não é a anterior aprovada.' }
if ((Get-FileHash -LiteralPath (Join-Path $pedGame 'gta_sa.exe') -Algorithm SHA256).Hash -ne 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26') { throw 'Hoodlum divergente.' }
foreach ($pedReserved in @($pedBackup, $pedLogBackup, $pedManifest)) {
    if (Test-Path -LiteralPath $pedReserved) { throw ('Evidência anterior já existe; preservar: ' + $pedReserved) }
}
$pedOtherBefore = ConvertTo-Json -InputObject @(Get-PedOtherAsis) -Depth 4
Assert-PedGtaClosed
Set-Content -LiteralPath $pedManifest -Value $pedOtherBefore -Encoding utf8
Assert-PedGtaClosed
Copy-Item -LiteralPath $pedTarget -Destination $pedBackup
if ((Get-FileHash -LiteralPath $pedBackup -Algorithm SHA256).Hash -ne $pedOldSha) { throw 'Backup divergente.' }
if (Test-Path -LiteralPath $pedLog) {
    Assert-PedGtaClosed
    Copy-Item -LiteralPath $pedLog -Destination $pedLogBackup
}
Assert-PedGtaClosed
Copy-Item -LiteralPath $pedSource -Destination $pedTarget -Force
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedNewSha) { throw 'Substituição divergente; não iniciar GTA.' }
if ((ConvertTo-Json -InputObject @(Get-PedOtherAsis) -Depth 4) -cne $pedOtherBefore) { throw 'Outro ASI divergiu; preservar evidência.' }
Assert-PedGtaClosed
```

O bloco não inicia GTA. Na sessão física aprovada pelo operador, manter o fluxo anterior de opt-in/F6 e filtrar o log pelo **novo PID**: o log existente não é apagado. Procurar `model_revalidate` e a primeira `spawn_rejected guard=...`; para playerReady, ler também `player_ready_* field=...`; para perda do modelo, `spawn_model_revalidation_failed`. Etapas posteriores à recusa não são executadas. Se houver spawn, confirmar CJ + segundo PED e remoção por F8. Não salvar nem trocar arquivos enquanto GTA estiver vivo. A identificação da guarda depende do novo log instrumentado; a build não substitui essa observação.
