# Alpha 0.4.1 — artefato habilitado e procedimento do operador

**Preparação concluída; aguardar autorização do operador antes de executar os blocos abaixo.** Nesta tarefa não houve GTA, cópia para scripts ou patch físico. A revisão da fase/ABI/thunk/cleanup é a registrada no Slice 0.1. O único comportamento alterado é o gate de compilação de um target separado. Um agente; sem commit/push.

## Artefatos e diff de habilitação

| Variante Debug/Win32 | Caminho relativo ao repositório | SHA-256 |
| --- | --- | --- |
| Habilitada, somente para este smoke | `build/alpha041-smoke-win32/client/windows/ped-smoke-enabled/Debug/GrandMPPedSmoke.asi` | `AE02EDF8F0C7AB9666AF29A8FC79BA2EA1778BFF8A1C0E1809F579DA2D341EFE` |
| Padrão, bloqueada | `build/alpha041-smoke-win32/client/windows/Debug/GrandMPPedSmoke.asi` | `7B83F8FE4DF6150B39D7108DC2F04A64CA12B87A8F63778123A5B2196C2143BE` |

`GRANDMP_BUILD_PED_SMOKE_ENABLED` é OFF por padrão. ON expõe `grandmp_ped_smoke_enabled`, ainda EXCLUDE_FROM_ALL. Só ele recebe `GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1`; a ASI padrão continua com `physicalExecutionApproved=false`. Os nomes de arquivo são iguais para preservar o caminho admitido, mas ASI/PDB/LIB/MAP habilitados saem em diretório próprio. O opt-in de runtime/F6 continua obrigatório. Não usar a ASI padrão esperando um PED.

Patch mínimo de habilitação: `build/alpha041-smoke/physical-enable.patch` (CMake do target e gate). Diff incremental completo desta tarefa: `build/alpha041-smoke/smoke-preparation.patch` (inclui testes e este procedimento), sobre a working tree recebida. `ped_smoke.cpp`, `ped_smoke_addresses.hpp`, `ped_smoke_phase.hpp` e `.def` conferidos byte a byte com a entrada da tarefa. Não houve alteração de endereços, ABI, thunk, streaming, callback, spawn ou remoção. Hashes e timestamps dos ASIs anteriores e da build fechada anterior também conferem.

## Build e verificações executadas

```powershell
Set-Location -LiteralPath 'D:\Programacao\Workspace\Applications\grandmp'
cmake -S . -B build/alpha041-smoke-win32 -G 'Visual Studio 18 2026' -A Win32 -DBUILD_TESTING=ON -DGRANDMP_BUILD_PED_SMOKE_ENABLED=ON
cmake --build build/alpha041-smoke-win32 --config Debug --target grandmp_ped_smoke grandmp_ped_smoke_tests grandmp_ped_smoke_enabled grandmp_ped_smoke_enabled_tests
ctest --test-dir build/alpha041-smoke-win32 -C Debug -R '^ped_smoke_' --output-on-failure
```

Build sem warnings. **22/22 testes passaram em 0,84 s: 11 padrão + 11 habilitados.** Mesmos casos de estados/one-shot/reentrância/remoção/identidade/ABI. Inicialização usa cada ASI real em host descartável: padrão termina bloqueada (status 2), habilitada recusa identidade/caminho do host (status 4), contador de callbacks zero em ambas, mesmo com opt-in e 16 inicializações concorrentes. O diagnóstico do PID do teste é conferido no log. Nenhuma simulação aceita a identidade do GTA ou alcança o writer.

Inspeção da **ASI habilitada final**, por PE32/MAP/dumpbin, em `build/alpha041-smoke/artifact-verification.json` e `artifact-gates-disasm.txt`:

- `originalCall` contém `E8 FF E2 1F 00`: 0x53C05C → 0x73A360. O original do thunk inicializa com 0x73A360; sem uso do CALL proibido.
- SHA Hoodlum reconstruído das 32 instruções de inicialização do comparador: `A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26`. BCrypt e comparador vinculados; caminhos exatos EXE/ASI presentes.
- Checks de TID e retorno 0x53C061; verificação de thread antes de VirtualProtect, FlushInstructionCache e conferência do CALL.
- Admissão antes do opt-in; consulta `GRANDMP_PED_SMOKE_ENABLE` e comparação, PID de primeiro plano e F6/0x75 antes da instalação. Corpo de spawn consome estado failed antes da única chamada create. Repetição/reentrância cobertas nos testes.

São verificações estáticas do artefato e testes sem GTA. F6/streaming/lifecycle/visibilidade reais só podem ser observados no smoke autorizado. Logs de build/CTest e script reproduzível de inspeção ficam no mesmo diretório. A existência de `ped_created` comprova as validações do código, não pixels na tela. O sucesso físico ainda não foi declarado.

## 1. Instalação — somente após autorização, GTA encerrado

Usar uma única sessão PowerShell para os três blocos. Não reconstruir o artefato depois de aprovar seu SHA. Não instalar Compact, debugger, contador ou outro hook para esse smoke. O loader ASI já configurado da cópia isolada deve chamar InitializeASI; não modificar os ASIs existentes.

```powershell
$ErrorActionPreference = 'Stop'
$pedRepo = 'D:\Programacao\Workspace\Applications\grandmp'
$pedGame = 'D:\Jogos\GrandMP-Smoke'
$pedScripts = Join-Path $pedGame 'scripts'
$pedExe = Join-Path $pedGame 'gta_sa.exe'
$pedSource = Join-Path $pedRepo 'build\alpha041-smoke-win32\client\windows\ped-smoke-enabled\Debug\GrandMPPedSmoke.asi'
$pedTarget = Join-Path $pedScripts 'GrandMPPedSmoke.asi'
$pedManifest = Join-Path $pedRepo 'build\alpha041-smoke\operator-asis-before.json'
$pedExpected = 'AE02EDF8F0C7AB9666AF29A8FC79BA2EA1778BFF8A1C0E1809F579DA2D341EFE'
$pedHoodlum = 'A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26'

function Assert-PedGtaClosed {
    if (Get-Process -Name 'gta_sa' -ErrorAction SilentlyContinue) {
        throw 'Encerre GTA normalmente antes de alterar qualquer arquivo.'
    }
}
function Get-PedAsiInventory {
    Get-ChildItem -LiteralPath $pedGame -Filter '*.asi' -File -Recurse |
        Sort-Object FullName | ForEach-Object {
            [pscustomobject]@{
                Path = $_.FullName
                Sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
                LastWriteTicks = $_.LastWriteTimeUtc.Ticks
            }
        }
}

Assert-PedGtaClosed
if (!(Test-Path -LiteralPath $pedScripts -PathType Container)) { throw 'scripts/loader isolado ausente; parar.' }
if (Test-Path -LiteralPath $pedTarget) { throw 'Experimento já instalado; não sobrescrever.' }
if (Test-Path -LiteralPath $pedManifest) { throw 'Manifesto de sessão anterior presente; arquive-o antes de iniciar outra sessão.' }
if ((Get-FileHash -LiteralPath $pedExe -Algorithm SHA256).Hash -ne $pedHoodlum) { throw 'Hoodlum divergente.' }
if ((Get-FileHash -LiteralPath $pedSource -Algorithm SHA256).Hash -ne $pedExpected) { throw 'ASI habilitada divergente.' }
$pedBeforeJson = ConvertTo-Json -InputObject @(Get-PedAsiInventory) -Depth 4
Assert-PedGtaClosed
Set-Content -LiteralPath $pedManifest -Value $pedBeforeJson -Encoding utf8
Assert-PedGtaClosed
Copy-Item -LiteralPath $pedSource -Destination $pedTarget
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedExpected) { throw 'Cópia divergente; não iniciar GTA.' }
```

## 2. Smoke — gameplay manual; somente leitura de logs enquanto vivo

```powershell
Assert-PedGtaClosed
if ((Get-FileHash -LiteralPath $pedExe -Algorithm SHA256).Hash -ne $pedHoodlum) { throw 'Hoodlum divergente.' }
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedExpected) { throw 'ASI divergente.' }
$env:GRANDMP_PED_SMOKE_ENABLE = '1'
try {
    $pedRun = Start-Process -FilePath $pedExe -WorkingDirectory $pedGame -WindowStyle Normal -PassThru
} finally {
    Remove-Item Env:GRANDMP_PED_SMOKE_ENABLE -ErrorAction SilentlyContinue
}
$pedLog = Join-Path $pedScripts 'GrandMPPedSmoke.log'
$pedSessionPattern = ' pid=' + $pedRun.Id + ' '
# Repetir este comando para ler apenas as linhas desta sessão:
if (Test-Path -LiteralPath $pedLog) {
    Get-Content -LiteralPath $pedLog | Where-Object { $_ -match $pedSessionPattern }
}
```

1. Carregar gameplay exterior, CJ vivo/a pé, terreno plano e livre, sem salvar. Manter o jogo em primeiro plano. Confirmar `loaded`, `preparation_read_only`, `press_F6_in_gameplay_to_install_candidate` no PID atual. F6 tem janela de **120 segundos a partir dessa mensagem**, incluindo tempo de loading. Se expirar ou houver failed, encerrar a sessão; não pressionar repetidamente nem mudar hooks.
2. Pressionar F6 uma vez, mantendo brevemente (~0,2 s) e soltando. Exigir `post_population_phase_installed_original_immutable_module_pinned`, `callback_executed`, `model_requested`, `model_loaded` e exatamente um `ped_created` do PID atual. A ordem relativa da mensagem de instalação e primeiro callback pode variar por serem threads diferentes.
3. Observar **CJ e um segundo PED simultaneamente na tela** (aproximadamente 2,5 m ao lado), jogo responsivo, sem nova criação ao aguardar/mover por alguns segundos. Posicionar a câmera manualmente se necessário. `cleanup_pending`/failed, ausência de PED ou criação repetida reprovam o smoke.
4. Pressionar F8 com gameplay em primeiro plano. Exigir um `removed` e desaparecimento somente do PED do experimento, CJ preservado e sem respawn. Encerrar GTA normalmente, sem salvar. Em falha, preservar o log e encerrar a sessão; não fazer hot-unload, reparo de bytes ou remoção de entidade desconhecida.

## 3. Remoção — GTA completamente encerrado

```powershell
Assert-PedGtaClosed
if ((Get-FileHash -LiteralPath $pedTarget -Algorithm SHA256).Hash -ne $pedExpected) {
    throw 'Arquivo não corresponde ao experimento instalado; não remover por suposição.'
}
Assert-PedGtaClosed
Remove-Item -LiteralPath $pedTarget
Remove-Item Env:GRANDMP_PED_SMOKE_ENABLE -ErrorAction SilentlyContinue
$pedAfterJson = ConvertTo-Json -InputObject @(Get-PedAsiInventory) -Depth 4
if ($pedAfterJson -cne (Get-Content -LiteralPath $pedManifest -Raw).TrimEnd()) {
    throw 'Inventário/hash/timestamp de ASIs anteriores divergente; preservar evidência.'
}
Assert-PedGtaClosed
if (Test-Path -LiteralPath $pedLog) {
    Copy-Item -LiteralPath $pedLog -Destination (Join-Path $pedRepo ('build\alpha041-smoke\operator-' + $pedRun.Id + '.log'))
}
# Manter manifesto e log para a revisão; nenhum ASI anterior foi removido/substituído.
```

**Estado da entrega: artefato pronto, execução não autorizada nesta tarefa. PARAR aqui para autorização do operador.** Resultado pendente único: CJ + segundo PED visível, seguido de remoção por F8.
