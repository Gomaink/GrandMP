# Primeiro PED local — variante para validar F8

**Preparada para revisão; nenhum bloco de instalação/smoke foi executado.** Fontes, CMake e testes aprovados permaneceram byte a byte iguais. Usado somente o mecanismo experimental existente, Debug/Win32, em diretório novo. Build padrão permanece bloqueada.

| Artefato | Caminho relativo ao repositório | SHA-256 |
| --- | --- | --- |
| Habilitado | `build/alpha04-ped-f8-win32/client/windows/ped-smoke-enabled/Debug/GrandMPPedSmoke.asi` | `DAA1272574CA0A6392435C7ADC41D4050C087F966B30662FF956995A9CC327B9` |
| Padrão bloqueado | `build/alpha04-ped-f8-win32/client/windows/Debug/GrandMPPedSmoke.asi` | `CF98AD0165F34C353D97FB2725936144D575F7BAEAC16FBCCE6659552B7B13D0` |

Build sem warnings. **32/32 testes focados passaram: 16 padrão + 16 habilitados, 1,12 s**, incluindo remoção normal, F8 por borda, falha terminal, perda de identidade, spawn único, diagnósticos e ABI. Nenhum teste fora de `^ped_smoke_` foi executado. Os hosts de inicialização confirmam bloqueio da padrão e recusa de identidade na habilitada, sem callback/engine no host descartável. A validação física de F8 continua pendente.

Logs/evidência em `build/alpha04-ped-f8/`: `ped-build.txt`, `ped-ctest.txt`, `artifact-verification.json`, `artifact-gates-disasm.txt`, snapshots de integridade e `handoff.json`. CALL 0x53C05C→0x73A360, SHA Hoodlum, thread/identidade, opt-in/F6, thunk e diagnósticos conferidos no artefato. As nove builds ASI anteriores e os três ASIs instalados foram conferidos por hash/timestamp, sem alterações. Sem GTA, escrita em GrandMP-Smoke, hook físico, commit/push.

## Substituição — somente após autorização, GTA encerrado

O SHA anterior `9B666254F414F2244FDEAD94678D436538384DAF9D93222A19271BA752EFA43F` foi observado por leitura na instalação. Se o arquivo mudar ou estiver ausente, o bloco para. Só `scripts/GrandMPPedSmoke.asi` é substituído; backup e log anterior vão para o workspace. Não usar outro artefato de mesmo nome.

```powershell
$ErrorActionPreference = 'Stop'
$pedRepo = 'D:\Programacao\Workspace\Applications\grandmp'
$pedGame = 'D:\Jogos\GrandMP-Smoke'
$pedEvidence = Join-Path $pedRepo 'build\alpha04-ped-f8'
$pedSource = Join-Path $pedRepo 'build\alpha04-ped-f8-win32\client\windows\ped-smoke-enabled\Debug\GrandMPPedSmoke.asi'
$pedTarget = Join-Path $pedGame 'scripts\GrandMPPedSmoke.asi'
$pedLog = Join-Path $pedGame 'scripts\GrandMPPedSmoke.log'
$pedBackup = Join-Path $pedEvidence 'GrandMPPedSmoke.previous.asi'
$pedLogBackup = Join-Path $pedEvidence 'GrandMPPedSmoke.previous.log'
$pedManifest = Join-Path $pedEvidence 'operator-other-asis-before.json'
$pedOldSha = '9B666254F414F2244FDEAD94678D436538384DAF9D93222A19271BA752EFA43F'
$pedNewSha = 'DAA1272574CA0A6392435C7ADC41D4050C087F966B30662FF956995A9CC327B9'
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
if (!(Test-Path -LiteralPath $pedTarget -PathType Leaf)) { throw 'ASI experimental anterior ausente; parar.' }
if ((Get-FileHash -LiteralPath $pedSource -Algorithm SHA256).Hash -ne $pedNewSha) { throw 'Nova ASI divergente.' }
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedOldSha) { throw 'ASI instalada divergente; não sobrescrever.' }
if ((Get-FileHash -LiteralPath (Join-Path $pedGame 'gta_sa.exe') -Algorithm SHA256).Hash -ne 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26') { throw 'Hoodlum divergente.' }
foreach ($pedReserved in @($pedBackup, $pedLogBackup, $pedManifest)) {
    if (Test-Path -LiteralPath $pedReserved) { throw ('Backup/evidência anterior já existe: ' + $pedReserved) }
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
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedNewSha) { throw 'Cópia divergente; não iniciar GTA.' }
if ((ConvertTo-Json -InputObject @(Get-PedOtherAsis) -Depth 4) -cne $pedOtherBefore) { throw 'Outro ASI divergiu; preservar evidência.' }
Assert-PedGtaClosed
```

## Smoke planejado — somente na sessão autorizada

1. Iniciar manualmente o GTA isolado com `GRANDMP_PED_SMOKE_ENABLE=1` no ambiente do processo. Não habilitar outro experimento. Registrar o PID e ler apenas suas linhas de `GrandMPPedSmoke.log` (o log é append).
2. Em gameplay exterior aberto, CJ vivo/a pé, pressionar F6 uma vez dentro da janela de 120 s indicada por `press_F6_in_gameplay_to_install_candidate`.
3. Confirmar PED visível ao lado de CJ e `ped_created`. **Sem afastar CJ nem morrer**, pressionar e soltar F8.
4. Confirmar `removed`, desaparecimento somente do PED experimental, CJ preservado e jogo responsivo. Aguardar alguns segundos para confirmar ausência de respawn. `ownership_lost`, `failed` ou falta de remoção reprovam este smoke de remoção normal; preservar o log sem retries/reparo/hot-unload.
5. Encerrar normalmente, sem salvar. Somente após o processo terminar, copiar o log para `build/alpha04-ped-f8/` e retirar o opt-in do shell. Nenhum arquivo do jogo deve ser trocado enquanto o processo estiver vivo.

**Parar para revisão/autorização.** Esta entrega não declara a remoção física por F8 validada e não inicia movimentação do PED.
